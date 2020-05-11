/* Copyright (C) 2015-2020, Wazuh Inc.
 * Copyright (C) 2009 Trend Micro Inc.
 * All right reserved.
 *
 * This program is free software; you can redistribute it
 * and/or modify it under the terms of the GNU General Public
 * License (version 2) as published by the FSF - Free Software
 * Foundation
 */

/* agent daemon */

#include "shared.h"
#include "agentd.h"

#ifndef ARGV0
#define ARGV0 "ossec-agentd"
#endif

int agent_debug_level;

/* Prototypes */
static void help_agentd(void) __attribute((noreturn));


/* Print help statement */
static void help_agentd()
{
    print_header();
    print_out("  %s: -[Vhdtf] [-u user] [-g group] [-c config]", ARGV0);
    print_out("    -V          Version and license message");
    print_out("    -h          This help message");
    print_out("    -d          Execute in debug mode. This parameter");
    print_out("                can be specified multiple times");
    print_out("                to increase the debug level.");
    print_out("    -t          Test configuration");
    print_out("    -f          Run in foreground");
    print_out("    -u <user>   User to run as (default: %s)", USER);
    print_out("    -g <group>  Group to run as (default: %s)", GROUPGLOBAL);
    print_out("    -c <config> Configuration file to use (default: %s)", DEFAULTCPATH);
    print_out(" ");
    exit(1);
}


int main(int argc, char **argv)
{
    int c = 0;
    int test_config = 0;
    int debug_level = 0;
    agent_debug_level = getDefine_Int("agent", "debug", 0, 2);

    const char *user = USER;
    const char *group = GROUPGLOBAL;
    const char *cfg = DEFAULTCPATH;

    uid_t uid;
    gid_t gid;

    run_foreground = 0;

    /* Set the name */
    OS_SetName(ARGV0);

    while ((c = getopt(argc, argv, "Vtdfhu:g:D:c:")) != -1) {
        switch (c) {
            case 'V':
                print_version();
                break;
            case 'h':
                help_agentd();
                break;
            case 'd':
                nowDebug();
                debug_level = 1;
                break;
            case 'f':
                run_foreground = 1;
                break;
            case 'u':
                if (!optarg) {
                    merror_exit("-u needs an argument");
                }
                user = optarg;
                break;
            case 'g':
                if (!optarg) {
                    merror_exit("-g needs an argument");
                }
                group = optarg;
                break;
            case 't':
                test_config = 1;
                break;
            case 'D':
                if (!optarg) {
                    merror_exit("-D needs an argument");
                }
                mwarn("-D is deprecated.");
                break;
            case 'c':
                if (!optarg) {
                    merror_exit("-c needs an argument.");
                }
                cfg = optarg;
                break;
            default:
                help_agentd();
                break;
        }
    }

    mdebug1(STARTED_MSG);

    agt = (agent *)calloc(1, sizeof(agent));
    if (!agt) {
        merror_exit(MEM_ERROR, errno, strerror(errno));
    }

    /* Check current debug_level
     * Command line setting takes precedence
     */
    if (debug_level == 0) {
        /* Get debug level */
        debug_level = agent_debug_level;
        while (debug_level != 0) {
            nowDebug();
            debug_level--;
        }
    }

    /* Read config */
    if (ClientConf(cfg) < 0) {
        merror_exit(CLIENT_ERROR);
    }

    if (!(agt->server && agt->server[0].rip)) {
        merror(AG_INV_IP);
        merror_exit(CLIENT_ERROR);
    }

    if (!Validate_Address(agt->server)){
        merror(AG_INV_MNGIP, agt->server[0].rip);
        merror_exit(CLIENT_ERROR);
    }

    if (agt->notify_time == 0) {
        agt->notify_time = NOTIFY_TIME;
    }
    if (agt->max_time_reconnect_try == 0 ) {
        agt->max_time_reconnect_try = RECONNECT_TIME;
    }
    if (agt->max_time_reconnect_try <= agt->notify_time) {
        agt->max_time_reconnect_try = (agt->notify_time * 3);
        minfo("Max time to reconnect can't be less than notify_time(%d), using notify_time*3 (%d)", agt->notify_time, agt->max_time_reconnect_try);
    }

    /* Check if the user/group given are valid */
    uid = Privsep_GetUser(user);
    gid = Privsep_GetGroup(group);
    if (uid == (uid_t) - 1 || gid == (gid_t) - 1) {
        merror_exit(USER_ERROR, user, group, strerror(errno), errno);
    }

    if(agt->enrollment_cfg && agt->enrollment_cfg->enabled) {
        // If autoenrollment is enabled, we will avoid exit if there is no valid key
        OS_PassEmptyKeyfile();
    } else {
        /* Check auth keys */
        if (!OS_CheckKeys()) {
            merror_exit(AG_NOKEYS_EXIT);
        }
    }
    
    /* Check client keys */
    OS_ReadKeys(&keys, 1, 0, 0);

    /* Check if we need to auto-enroll */
    if(agt->enrollment_cfg && agt->enrollment_cfg->enabled && keys.keysize == 0) {
        int could_register = -1;
        int rc = 0;

        if (agt->enrollment_cfg->target_cfg->manager_name) {
            // Configured enrollment server
            could_register = w_enrollment_request_key(agt->enrollment_cfg, agt->enrollment_cfg->target_cfg->manager_name);
        } 
        
        // Try to enroll to server list
        while (agt->server[rc].rip && (could_register != 0)) {
            could_register = w_enrollment_request_key(agt->enrollment_cfg, agt->server[rc].rip);
            rc++;
        }
        

        if(could_register == 0) {
            // Wait for key update on agent side
            mdebug1("Sleeping %d seconds to allow manager key file updates", agt->enrollment_cfg->delay_after_enrollment);
            sleep(agt->enrollment_cfg->delay_after_enrollment);
            // Readkeys again to add obtained key
            OS_ReadKeys(&keys, 1, 0, 0);
        } else {
            merror_exit(AG_ENROLL_FAIL);
        }
    }

    /* Exit if test config */
    if (test_config) {
        exit(0);
    }

    /* Start the signal manipulation */
    StartSIG(ARGV0);

    /* Agentd Start */
    AgentdStart(uid, gid, user, group);

    return (0);
}
