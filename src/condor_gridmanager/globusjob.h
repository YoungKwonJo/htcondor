/***************************Copyright-DO-NOT-REMOVE-THIS-LINE**
 * CONDOR Copyright Notice
 *
 * See LICENSE.TXT for additional notices and disclaimers.
 *
 * Copyright (c)1990-1998 CONDOR Team, Computer Sciences Department, 
 * University of Wisconsin-Madison, Madison, WI.  All Rights Reserved.  
 * No use of the CONDOR Software Program Source Code is authorized 
 * without the express consent of the CONDOR Team.  For more information 
 * contact: CONDOR Team, Attention: Professor Miron Livny, 
 * 7367 Computer Sciences, 1210 W. Dayton St., Madison, WI 53706-1685, 
 * (608) 262-0856 or miron@cs.wisc.edu.
 *
 * U.S. Government Rights Restrictions: Use, duplication, or disclosure 
 * by the U.S. Government is subject to restrictions as set forth in 
 * subparagraph (c)(1)(ii) of The Rights in Technical Data and Computer 
 * Software clause at DFARS 252.227-7013 or subparagraphs (c)(1) and 
 * (2) of Commercial Computer Software-Restricted Rights at 48 CFR 
 * 52.227-19, as applicable, CONDOR Team, Attention: Professor Miron 
 * Livny, 7367 Computer Sciences, 1210 W. Dayton St., Madison, 
 * WI 53706-1685, (608) 262-0856 or miron@cs.wisc.edu.
****************************Copyright-DO-NOT-REMOVE-THIS-LINE**/

#ifndef GLOBUSJOB_H
#define GLOBUSJOB_H

#include "condor_common.h"
#include "condor_classad.h"
#include "MyString.h"
#include "globus_utils.h"

#include "proxymanager.h"
#include "globusresource.h"
#include "gahp-client.h"

#define JM_COMMIT_TIMEOUT	600

class GlobusResource;

class GlobusJob : public Service
{
 public:
	GlobusJob( ClassAd *classad, GlobusResource *resource );

	~GlobusJob();

	void Reconfig();
	void SetEvaluateState();
	int doEvaluateState();
	int CommunicationTimeout();
	void NotifyResourceDown();
	void NotifyResourceUp();
	void UpdateCondorState( int new_state );
	void UpdateGlobusState( int new_state, int new_error_code );
	void GramCallback( int new_state, int new_error_code );
	bool GetCallbacks();
	void ClearCallbacks();
	GlobusResource *GetResource();

	/* If true, then ATTR_ON_EXIT_BY_SIGNAL, ATTR_ON_EXIT_SIGNAL, and
	   ATTR_ON_EXIT_CODE are valid.  If false, no exit status is available.
	   At the moment this only returns true for gridshell jobs.

	   This is virtual as a reminder for when we merge in Jaime's gridmanager
	   reorg.  GlobusJob derives from a base Job class.  I imagine that the
	   base job class will provide a virtual version that simply returns false,
	   and GlobusJob will return true if the grid shell is active.  Heck, maybe
	   gridshell jobs should be "class GlobusJobGridshell : public GlobusJob",
	   but that only makes sense if the gridshell is permenantly bound to
	   GlobusJob.
	*/
	virtual bool IsExitStatusValid();

	static int probeInterval;
	static int submitInterval;
	static int restartInterval;
	static int gahpCallTimeout;
	static int maxConnectFailures;
	static int outputWaitGrowthTimeout;

	static void setProbeInterval( int new_interval )
		{ probeInterval = new_interval; }
	static void setSubmitInterval( int new_interval )
		{ submitInterval = new_interval; }
	static void setRestartInterval( int new_interval )
		{ restartInterval = new_interval; }
	static void setGahpCallTimeout( int new_timeout )
		{ gahpCallTimeout = new_timeout; }
	static void setConnectFailureRetry( int count )
		{ maxConnectFailures = count; }

	// New variables
	bool resourceDown;
	bool resourceStateKnown;
	int condorState;
	int gmState;
	int globusState;
	int globusStateErrorCode;
	int globusStateBeforeFailure;
	int callbackGlobusState;
	int callbackGlobusStateErrorCode;
	bool jmUnreachable;
	GlobusResource *myResource;
	int evaluateStateTid;
	int communicationTimeoutTid;
	time_t lastProbeTime;
	bool probeNow;
	time_t enteredCurrentGmState;
	time_t enteredCurrentGlobusState;
	time_t lastSubmitAttempt;
	int numSubmitAttempts;
	int submitFailureCode;
	int lastRestartReason;
	time_t lastRestartAttempt;
	int numRestartAttempts;
	int numRestartAttemptsThisSubmit;
	time_t jmProxyExpireTime;
	time_t outputWaitLastGrowth;
	int outputWaitOutputSize;
	int outputWaitErrorSize;
	// HACK!
	bool retryStdioSize;
	char *resourceManagerString;
	bool useGridJobMonitor;

	bool gahp_proxy_id_set;
	Proxy *myProxy;
	GahpClient gahp;

	MyString *buildSubmitRSL();
	MyString *buildRestartRSL();
	MyString *buildStdioUpdateRSL();
	bool GetOutputSize( int& output, int& error );
	void DeleteOutput();

	void UpdateJobAd( const char *name, const char *value );
	void UpdateJobAdInt( const char *name, int value );
	void UpdateJobAdFloat( const char *name, float value );
	void UpdateJobAdBool( const char *name, int value );
	void UpdateJobAdString( const char *name, const char *value );

	PROC_ID procID;
	char *jobContact;
		// If we're in the middle of a globus call that requires an RSL,
		// the RSL is stored here (so that we don't have to reconstruct the
		// RSL every time we test the call for completion). It should be
		// freed and reset to NULL once the call completes.
	MyString *RSL;
	MyString errorString;
	char *localOutput;
	char *localError;
	bool streamOutput;
	bool streamError;
	bool stageOutput;
	bool stageError;
	int globusError;
	bool submitLogged;
	bool executeLogged;
	bool submitFailedLogged;
	bool terminateLogged;
	bool abortLogged;
	bool evictLogged;
	bool holdLogged;

	bool stateChanged;
	int jmVersion;
	bool restartingJM;
	time_t restartWhen;

	ClassAd *ad;

	int wantResubmit;
	int doResubmit;
	int wantRematch;
	int numGlobusSubmits;

	MyString outputClassadFilename;
	bool useGridShell;

 protected:
	bool callbackRegistered;
	int connect_failure_counter;
	bool AllowTransition( int new_state, int old_state );

	bool FailureIsRestartable( int error_code );
	bool FailureNeedsCommit( int error_code );
	bool JmShouldSleep();

private:
	// Copy constructor not implemented.  Don't call.
	GlobusJob( GlobusJob& copy );
};

#endif

