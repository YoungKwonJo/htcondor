/***************************************************************
 *
 * Copyright (C) 1990-2007, Condor Team, Computer Sciences Department,
 * University of Wisconsin-Madison, WI.
 * 
 * Licensed under the Apache License, Version 2.0 (the "License"); you
 * may not use this file except in compliance with the License.  You may
 * obtain a copy of the License at
 * 
 *    http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 ***************************************************************/

#include "condor_common.h"
#include <vector>
#include <iostream>
#include <string>
#include "condor_random_num.h"
#include <cstring>
#include <fstream>
#include <curl/curl.h>
#include <cstdlib>

// Class to support RAII technique for curl
class Curl {
public:
	Curl() { curlp = curl_easy_init(); }
	~Curl() { if( curlp ) { curl_easy_cleanup( curlp ); } }
	CURL* get() { return curlp; }
private:
	CURL* curlp;
};

// Class to support RAII technique for curl slist
class Curl_slist {
public:
	Curl_slist() { slist = NULL; }
	~Curl_slist() { if( slist ) { curl_slist_free_all( slist ); } }
	struct curl_slist* get() { return slist; }
	struct curl_slist* append( const char* s );
private:
	struct curl_slist* slist;
};

struct curl_slist* Curl_slist::append( const char* s )
{
	return slist = curl_slist_append( slist, s );
}

// The default url we send to
const char* cc_metrics_url = "http://metrics.pegasus.isi.edu/metrics";

// Will only try for about 100 seconds, then die
// Configured at the command line from DAGMan, using the
// configuration variable DAGMAN_PEGASUS_REPORT_TIMEOUT
const char default_duration = 100;

// Separators are space and ','
// This list generated by the following code
// #include <iostream>
// #include <cctype>
//
// int main(int argc,char* argv[])
// {
// 	for(int ii=0;ii<0x80;) {
// 		if(std::isspace(ii)) std::cout << '1';
// 		else if(ii == int(',')) std::cout << '1';
// 		else std::cout << '0';
// 		std::cout << ',';
// 		if((++ii % 8) == 0) std::cout << '\n';
// 	}
// 	return 0;
// }

int sep[] = {
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 1, 1, 1, 1, 1, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	1, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 1, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0
};

struct server_data {
	std::string server;
	bool connected;
	server_data() : connected( false ) {}
	server_data( const char* p ) : server( p ), connected( false ) {}
	server_data( const std::string& p ) : server( p ), connected( false ) {}
};

// We are assuming that the environment variable is specified as, say
//
// PEGASUS_USER_METRICS_SERVER=http://localhost:4001,http://another.server.edu
//
// or
//
// PEGASUS_USER_METRICS_SERVER="http://localhost:4001 http://another.server.edu"
//
// That is, each server url is separated by either whitespace or a comma
//

void parse_metrics_server_env( std::vector<server_data>* servers, const char* list )
{
	if( !list ) {
		return;
	}
	server_data server;
	for( ; *list; ++list ) {
		if( sep[ int( *list ) ] ) {
			if( !server.server.empty() ) {
				servers->push_back( server );
				server.server.clear();
			}
		} else {
			server.server.append( 1, *list );
		}
	}
	if( !server.server.empty() ) {
		servers->push_back( server );
	}
}

int main( int argc, char* argv[] )
{
	std::string metrics_from_dagman, metrics_url;
	bool do_sleep = false;
	int duration = default_duration;

	std::vector<server_data> servers_to_contact;
	char* env_metrics_server = getenv( "PEGASUS_USER_METRICS_SERVER" );
	if( env_metrics_server ) {
		parse_metrics_server_env( &servers_to_contact, env_metrics_server );
	}
		// Pull parameters off the command line
	for( int ii = 1; ii < argc; ++ii ) {
		if( !std::strcmp( argv[ii], "-f" ) ) {
			++ii;
			if( argv[ii] ) {
				metrics_from_dagman = argv[ii];
			} else {
				std::cerr << "No metrics file specified" << std::endl;
			}
		}
			// -s means we should sleep for a random amount of time
			// so we don't clobber the server if a bunch of
			// sub-DAGs have been condor_rm'ed at once.
		if( !std::strcmp( argv[ii], "-s" ) ) {
			do_sleep = true;
		}
		if( !std::strcmp( argv[ii], "-u" ) ) { // For testing
			++ii;
			if( argv[ii] ) {
				metrics_url = argv[ii];
			} else {
				std::cerr << "No metrics URL specified" << std::endl;
			}
		}
		if( !std::strcmp( argv[ii], "-t" ) ) {
			++ii;
			if( argv[ii] ) {
				duration = std::atoi( argv[ii] );
			}
		}
	}
		// Now check the command line parameters
	std::cout << "Executing:";
	for( char**p = &argv[0]; *p; ++p ) {
		std::cout << " \"" << *p << "\"";
	}
	std::cout << std::endl;
	if( metrics_from_dagman.empty() ) {
		std::cout << "Metrics from dagman is not specified. Terminating" << std::endl;
		return 1;
	}
	if( metrics_url.empty() ) {
		char* default_metrics_server = getenv( "PEGASUS_USER_METRICS_DEFAULT_SERVER" );
		if( default_metrics_server ) {
			servers_to_contact.push_back( server_data( default_metrics_server ) );
		} else {
			servers_to_contact.push_back( server_data( cc_metrics_url ) );
		}
	} else {
		servers_to_contact.push_back( server_data( metrics_url ) );
	}
	std::cout << std::endl <<
				"Will attempt to contact the following metrics servers:" <<
				std::endl;
	for( std::vector<server_data>::iterator p = servers_to_contact.begin();
	        p != servers_to_contact.end(); ++p ) {
		std::cout << '\t' << p->server << std::endl;
	}

	bool status = false;
	time_t stop_time;
	time( &stop_time );
	stop_time += duration;

	Curl handle;
	Curl_slist slist;
	if( !handle.get() ) {
		std::cout << "Failed to initialize curl. Nothing to do" << std::endl;
		return 1;
	}
	if( !slist.append( "Content-Type: application/json" ) ) {
		std::cout << "Failed to set header" << std::endl;
		return 1;
	}
	std::ifstream metrics( metrics_from_dagman.c_str() );
	if( !metrics ) {
		std::cout << "Failed to open " << metrics_from_dagman << " for reading" << std::endl;
		return 1;
	}
	std::string data_to_send;
	std::string data_line;

		// Slurp all the data into a single string
	while( getline( metrics, data_line ) ) {
		std::string::iterator p = data_line.begin();
		for( ; p != data_line.end(); ++p ) {
			if( !isspace( *p ) ) {
				break;
			}
		}
		data_line.erase( data_line.begin(), p );
		std::string::reverse_iterator q = data_line.rbegin();
		for( ; q != data_line.rend(); ++q ) {
			if( !isspace( *q ) ) {
				break;
			}
		}
		data_line.erase( q.base(), data_line.end() );

		data_to_send.append( data_line );
	}
	metrics.close();
	std::cout << std::endl << "Data to send: <" << data_to_send <<
				">" << std::endl << std::endl;

		// Now set curl options
	if( CURLcode res = curl_easy_setopt( handle.get(), CURLOPT_POST, 1 ) ) {
		std::cout << "Failed  to set POST option" << std::endl;
		std::cout << curl_easy_strerror(res) << std::endl;
		return 1;
	}
	if( CURLcode res = curl_easy_setopt( handle.get(), CURLOPT_POSTFIELDS, data_to_send.data() ) ) {
		std::cout << "Failed to set data to send in POST" << std::endl;
		std::cout << curl_easy_strerror(res) << std::endl;
		return 1;
	}
	if( CURLcode res = curl_easy_setopt( handle.get(), CURLOPT_POSTFIELDSIZE, data_to_send.size() ) ) {
		std::cout << "Failed to set data size to send in POST" << std::endl;
		std::cout << curl_easy_strerror(res) << std::endl;
		return 1;
	}
	if( CURLcode res = curl_easy_setopt( handle.get(), CURLOPT_HTTPHEADER, slist.get() ) ) {
		std::cout << "Failed to set header option to use json" << std::endl;
		std::cout << curl_easy_strerror(res) << std::endl;
		return 1;
	}
	char error_buffer[CURL_ERROR_SIZE];
	if( CURLcode res = curl_easy_setopt( handle.get(), CURLOPT_ERRORBUFFER, &error_buffer[0] ) ) {
		std::cout << "Failed to set error buffer" << std::endl;
		std::cout << curl_easy_strerror(res) << std::endl;
		return 1;
	}
		// Design document says try until 100 seconds are up
	do {
		for( std::vector<server_data>::iterator srv = servers_to_contact.begin();
				srv != servers_to_contact.end(); ++srv ) {
			if( srv->connected ) continue;
			if( do_sleep ) {
					// TEMP:  Sleep duration should be configurable via
					// the environment.
				sleep( 1 + ( get_random_int() % 10 ) );
			}

			if( CURLcode res = curl_easy_setopt( handle.get(), CURLOPT_URL, srv->server.c_str() ) ) {
				std::cout << "Failed to set URL to send to" << std::endl;
				std::cout << curl_easy_strerror(res) << std::endl;
				continue;
			}
				// Finally, send the data
			CURLcode res = curl_easy_perform( handle.get() );
			if( !res ) {
				std::cout << "Successfully sent data to server " << srv->server << std::endl;
				srv->connected = true;
			} else {
				std::cout << "Failed to send data to " << srv->server << std::endl;
				std::cout << "curl_easy_perform failed with code " << res << std::endl;
				std::cout << "Curl says: " << error_buffer << std::endl;
			}
		}
		status = true;
		for( std::vector<server_data>::iterator srv = servers_to_contact.begin();
				srv != servers_to_contact.end(); ++srv ) {
			if( !srv->connected ) {
				status = false;
				break;
			}
		}
	} while( !status && time( 0 ) < stop_time );
	return status ? 0 : 1;
}
