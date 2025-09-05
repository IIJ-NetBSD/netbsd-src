/*	$NetBSD: main.c,v 1.4 2025/09/05 21:16:25 christos Exp $	*/

/* $OpenLDAP$ */
/* This work is part of OpenLDAP Software <http://www.openldap.org/>.
 *
 * Copyright 1998-2024 The OpenLDAP Foundation.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted only as authorized by the OpenLDAP
 * Public License.
 *
 * A copy of this license is available in the file LICENSE in the
 * top-level directory of the distribution or, alternatively, at
 * <http://www.OpenLDAP.org/license.html>.
 */
/* Portions Copyright (c) 1995 Regents of the University of Michigan.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that this notice is preserved and that due credit is given
 * to the University of Michigan at Ann Arbor. The name of the University
 * may not be used to endorse or promote products derived from this
 * software without specific prior written permission. This software
 * is provided ``as is'' without express or implied warranty.
 */

#include <sys/cdefs.h>
__RCSID("$NetBSD: main.c,v 1.4 2025/09/05 21:16:25 christos Exp $");

#include "portable.h"

#include <stdio.h>

#include <ac/ctype.h>
#include <ac/socket.h>
#include <ac/string.h>
#include <ac/time.h>
#include <ac/unistd.h>
#include <ac/wait.h>
#include <ac/errno.h>

#include "slap.h"
#include "lutil.h"
#include "ldif.h"

#ifdef LDAP_SLAPI
#include "slapi/slapi.h"
#endif

#ifdef LDAP_SIGCHLD
static RETSIGTYPE wait4child( int sig );
#endif

#ifdef HAVE_NT_SERVICE_MANAGER
#define MAIN_RETURN(x) return
static struct sockaddr_in	bind_addr;

#define SERVICE_EXIT( e, n )	do { \
	if ( is_NT_Service ) { \
		lutil_ServiceStatus.dwWin32ExitCode				= (e); \
		lutil_ServiceStatus.dwServiceSpecificExitCode	= (n); \
	} \
} while ( 0 )

#else
#define SERVICE_EXIT( e, n )
#define MAIN_RETURN(x) return(x)
#endif

typedef int (MainFunc) LDAP_P(( int argc, char *argv[] ));
extern MainFunc slapadd, slapcat, slapdn, slapindex, slappasswd,
	slaptest, slapauth, slapacl, slapschema, slapmodify;

static struct {
	char *name;
	MainFunc *func;
} tools[] = {
	{"slapadd", slapadd},
	{"slapcat", slapcat},
	{"slapdn", slapdn},
	{"slapindex", slapindex},
	{"slapmodify", slapmodify},
	{"slappasswd", slappasswd},
	{"slapschema", slapschema},
	{"slaptest", slaptest},
	{"slapauth", slapauth},
	{"slapacl", slapacl},
	/* NOTE: new tools must be added in chronological order,
	 * not in alphabetical order, because for backwards
	 * compatibility name[4] is used to identify the
	 * tools; so name[4]=='a' must refer to "slapadd" and
	 * not to "slapauth".  Alphabetical order can be used
	 * for tools whose name[4] is not used yet */
	{NULL, NULL}
};

/*
 * when more than one slapd is running on one machine, each one might have
 * it's own LOCAL for syslogging and must have its own pid/args files
 */

#ifndef HAVE_MKVERSION
const char Versionstr[] =
	OPENLDAP_PACKAGE " " OPENLDAP_VERSION " Standalone LDAP Server (slapd)";
#endif

extern OverlayInit slap_oinfo[];
extern BackendInfo slap_binfo[];

#define	CHECK_NONE	0x00
#define	CHECK_CONFIG	0x01
#define	CHECK_LOGLEVEL	0x02
static int check = CHECK_NONE;
static int version = 0;

void *slap_tls_ctx;
LDAP *slap_tls_ld;

static int
slapd_opt_slp( const char *val, void *arg )
{
#ifdef HAVE_SLP
	/* NULL is default */
	if ( val == NULL || *val == '(' || strcasecmp( val, "on" ) == 0 ) {
		slapd_register_slp = 1;
		slapd_slp_attrs = (val != NULL && *val == '(') ? val : NULL;

	} else if ( strcasecmp( val, "off" ) == 0 ) {
		slapd_register_slp = 0;

	/* NOTE: add support for URL specification? */

	} else {
		fprintf(stderr, "unrecognized value \"%s\" for SLP option\n", val );
		return -1;
	}

	return 0;
		
#else
	fputs( "slapd: SLP support is not available\n", stderr );
	return 0;
#endif
}

/*
 * Option helper structure:
 * 
 * oh_nam	is left-hand part of <option>[=<value>]
 * oh_fnc	is handler function
 * oh_arg	is an optional arg to oh_fnc
 * oh_usage	is the one-line usage string related to the option,
 *		which is assumed to start with <option>[=<value>]
 *
 * please leave valid options in the structure, and optionally #ifdef
 * their processing inside the helper, so that reasonable and helpful
 * error messages can be generated if a disabled option is requested.
 */
struct option_helper {
	struct berval	oh_name;
	int		(*oh_fnc)(const char *val, void *arg);
	void		*oh_arg;
	const char	*oh_usage;
} option_helpers[] = {
	{ BER_BVC("slp"),	slapd_opt_slp,	NULL, "slp[={on|off|(attrs)}] enable/disable SLP using (attrs)" },
	{ BER_BVNULL, 0, NULL, NULL }
};

static void
usage( char *name )
{
	fprintf( stderr,
		"usage: %s options\n", name );
	fprintf( stderr,
		"\t-4\t\tIPv4 only\n"
#ifdef LDAP_PF_INET6
		"\t-6\t\tIPv6 only\n"
#endif
		"\t-T {acl|add|auth|cat|dn|index|modify|passwd|test}\n"
		"\t\t\tRun in Tool mode\n"
		"\t-c cookie\tSync cookie of consumer\n"
		"\t-d level\tDebug level" "\n"
		"\t-f filename\tConfiguration file\n"
		"\t-F dir\tConfiguration directory\n"
#if defined(HAVE_SETUID) && defined(HAVE_SETGID)
		"\t-g group\tGroup (id or name) to run as\n"
#endif
		"\t-h URLs\t\tList of URLs to serve\n"
#ifdef SLAP_DEFAULT_SYSLOG_USER
		"\t-l facility\tSyslog facility (default: LOCAL4)\n"
#endif
		"\t-n serverName\tService name\n"
		"\t-o <opt>[=val] generic means to specify options" );
	if ( !BER_BVISNULL( &option_helpers[0].oh_name ) ) {
		int	i;

		fprintf( stderr, "; supported options:\n" );
		for ( i = 0; !BER_BVISNULL( &option_helpers[i].oh_name ); i++) {
			fprintf( stderr, "\t\t%s\n", option_helpers[i].oh_usage );
		}
	} else {
		fprintf( stderr, "\n" );
	}
	fprintf( stderr,	
#ifdef HAVE_CHROOT
		"\t-r directory\tSandbox directory to chroot to\n"
#endif
		"\t-s level\tSyslog level\n"
#if defined(HAVE_SETUID) && defined(HAVE_SETGID)
		"\t-u user\t\tUser (id or name) to run as\n"
#endif
		"\t-V\t\tprint version info (-VV exit afterwards, -VVV print\n"
		"\t\t\tinfo about static overlays and backends)\n"
    );
}

#ifdef HAVE_NT_SERVICE_MANAGER
void WINAPI ServiceMain( DWORD argc, LPTSTR *argv )
#else
int main( int argc, char **argv )
#endif
{
	int		i, no_detach = 0;
	int		rc = 1;
	char *urls = NULL;
#if defined(HAVE_SETUID) && defined(HAVE_SETGID)
	char *username = NULL;
	char *groupname = NULL;
#endif
#if defined(HAVE_CHROOT)
	char *sandbox = NULL;
#endif
#ifdef SLAP_DEFAULT_SYSLOG_USER
	int syslogUser = SLAP_DEFAULT_SYSLOG_USER;
#endif
	
#ifndef HAVE_WINSOCK
	int pid, waitfds[2];
#endif
	int g_argc = argc;
	char **g_argv = argv;

	char *configfile = NULL;
	char *configdir = NULL;
	int serverMode = SLAP_SERVER_MODE;

	struct sync_cookie *scp = NULL;
	struct sync_cookie *scp_entry = NULL;

	char *serverNamePrefix = "";
	size_t	l;

	int slapd_pid_file_unlink = 0, slapd_args_file_unlink = 0;
	int firstopt = 1;

#ifdef CSRIMALLOC
	FILE *leakfile;
	if( ( leakfile = fopen( "slapd.leak", "w" )) == NULL ) {
		leakfile = stderr;
	}
#endif

	slap_sl_mem_init();

	(void) ldap_pvt_thread_initialize();
	ldap_pvt_thread_mutex_init( &logfile_mutex );

#ifdef HAVE_TLS
	rc = ldap_create( &slap_tls_ld );
	if ( rc ) {
		MAIN_RETURN( rc );
	}
	/* Library defaults to full certificate checking. This is correct when
	 * a client is verifying a server because all servers should have a
	 * valid cert. But few clients have valid certs, so we want our default
	 * to be no checking. The config file can override this as usual.
	 */
	rc = LDAP_OPT_X_TLS_NEVER;
	(void) ldap_pvt_tls_set_option( slap_tls_ld, LDAP_OPT_X_TLS_REQUIRE_CERT, &rc );
#endif

	global_host = ldap_pvt_get_fqdn( NULL );
	ber_str2bv( global_host, 0, 0, &global_host_bv );

	serverName = lutil_progname( "slapd", argc, argv );

	if ( strcmp( serverName, "slapd" ) ) {
#ifdef DEBUG_CLOSE
		extern void slapd_debug_close();
		slapd_debug_close();
#endif
		for (i=0; tools[i].name; i++) {
			if ( !strcmp( serverName, tools[i].name ) ) {
				rc = tools[i].func(argc, argv);
				MAIN_RETURN(rc);
			}
		}
	}

#ifdef HAVE_NT_SERVICE_MANAGER
	{
		int *ip;
		char *newConfigFile;
		char *newConfigDir;
		char *newUrls;
		char *regService = NULL;

		if ( is_NT_Service ) {
			lutil_CommenceStartupProcessing( serverName, slap_sig_shutdown );
			if ( strcmp(serverName, SERVICE_NAME) )
			    regService = serverName;
		}

		ip = (int*)lutil_getRegParam( regService, "DebugLevel" );
		if ( ip != NULL ) {
			slap_debug = *ip;
			Debug( LDAP_DEBUG_ANY,
				"new debug level from registry is: %d\n", slap_debug );
		}

		newUrls = (char *) lutil_getRegParam(regService, "Urls");
		if (newUrls) {
		    urls = ch_strdup(newUrls);
		    Debug(LDAP_DEBUG_ANY, "new urls from registry: %s\n",
				urls );
		}

		newConfigFile = (char*)lutil_getRegParam( regService, "ConfigFile" );
		if ( newConfigFile != NULL ) {
			configfile = ch_strdup(newConfigFile);
			Debug ( LDAP_DEBUG_ANY, "new config file from registry is: %s\n", configfile );
		}

		newConfigDir = (char*)lutil_getRegParam( regService, "ConfigDir" );
		if ( newConfigDir != NULL ) {
			configdir = ch_strdup(newConfigDir);
			Debug ( LDAP_DEBUG_ANY, "new config dir from registry is: %s\n", configdir );
		}
	}
#endif

	while ( (i = getopt( argc, argv,
			     "c:d:f:F:h:n:o:s:tT:V"
#ifdef LDAP_PF_INET6
				"46"
#endif
#ifdef HAVE_CHROOT
				"r:"
#endif
#if defined(LDAP_DEBUG) && defined(LDAP_SYSLOG)
				"S:"
#ifdef LOG_LOCAL4
				"l:"
#endif
#endif
#if defined(HAVE_SETUID) && defined(HAVE_SETGID)
				"u:g:"
#endif
			     )) != EOF ) {
		switch ( i ) {
		case '4':
			slap_inet4or6 = AF_INET;
			break;
#ifdef LDAP_PF_INET6
		case '6':
			slap_inet4or6 = AF_INET6;
			break;
#endif

		case 'h':	/* listen URLs */
			urls = optarg;
			break;

		case 'c':	/* provide sync cookie, override if exist in consumer */
			scp = (struct sync_cookie *) ch_calloc( 1,
										sizeof( struct sync_cookie ));
			ber_str2bv( optarg, 0, 1, &scp->octet_str );
			
			/* This only parses out the rid at this point */
			slap_parse_sync_cookie( scp, NULL );

			if ( scp->rid == -1 ) {
				Debug( LDAP_DEBUG_ANY,
						"main: invalid cookie \"%s\"\n",
						optarg );
				slap_sync_cookie_free( scp, 1 );
				goto destroy;
			}

			LDAP_STAILQ_FOREACH( scp_entry, &slap_sync_cookie, sc_next ) {
				if ( scp->rid == scp_entry->rid ) {
					Debug( LDAP_DEBUG_ANY,
						    "main: duplicated replica id in cookies\n" );
					slap_sync_cookie_free( scp, 1 );
					goto destroy;
				}
			}
			LDAP_STAILQ_INSERT_TAIL( &slap_sync_cookie, scp, sc_next );
			break;

		case 'd': {	/* set debug level and 'do not detach' flag */
			int	level = 0;

			if ( strcmp( optarg, "?" ) == 0 ) {
				check |= CHECK_LOGLEVEL;
				break;
			}

			no_detach = 1;
			if ( slap_parse_debug_level( optarg, &level, 0 ) ) {
				goto destroy;
			}
#ifdef LDAP_DEBUG
			slap_debug |= level;
#else
			if ( level != 0 )
				fputs( "must compile with LDAP_DEBUG for debugging\n",
				       stderr );
#endif
			} break;

		case 'f':	/* read config file */
			configfile = optarg;
			break;

		case 'F':	/* use config dir */
			configdir = optarg;
			break;

		case 'o': {
			char		*val = strchr( optarg, '=' );
			struct berval	opt;

			opt.bv_val = optarg;
			
			if ( val ) {
				opt.bv_len = ( val - optarg );
				val++;
			
			} else {
				opt.bv_len = strlen( optarg );
			}

			for ( i = 0; !BER_BVISNULL( &option_helpers[i].oh_name ); i++ ) {
				if ( ber_bvstrcasecmp( &option_helpers[i].oh_name, &opt ) == 0 ) {
					assert( option_helpers[i].oh_fnc != NULL );
					if ( (*option_helpers[i].oh_fnc)( val, option_helpers[i].oh_arg ) == -1 ) {
						/* we assume the option parsing helper
						 * issues appropriate and self-explanatory
						 * error messages... */
						goto stop;
					}
					break;
				}
			}

			if ( BER_BVISNULL( &option_helpers[i].oh_name ) ) {
				goto unhandled_option;
			}
			break;
		}

		case 's':	/* set syslog level */
			if ( strcmp( optarg, "?" ) == 0 ) {
				check |= CHECK_LOGLEVEL;
				break;
			}

			if ( slap_parse_debug_level( optarg, &ldap_syslog, 1 ) ) {
				goto destroy;
			}
			break;

#if defined(LDAP_DEBUG) && defined(LDAP_SYSLOG)
		case 'S':
			if ( slap_parse_syslog_level( optarg, &ldap_syslog_level ) ) {
				goto destroy;
			}
			break;

#ifdef LOG_LOCAL4
		case 'l':	/* set syslog local user */
			if ( slap_parse_syslog_user( optarg, &syslogUser ) ) {
				goto destroy;
			}
			break;
#endif
#endif /* LDAP_DEBUG && LDAP_SYSLOG */

#ifdef HAVE_CHROOT
		case 'r':
			sandbox = optarg;
			break;
#endif

#if defined(HAVE_SETUID) && defined(HAVE_SETGID)
		case 'u':	/* user name */
			username = optarg;
			break;

		case 'g':	/* group name */
			groupname = optarg;
			break;
#endif /* SETUID && GETUID */

		case 'n':  /* NT service name */
			serverName = optarg;
			break;

		case 't':
			/* deprecated; use slaptest instead */
			fprintf( stderr, "option -t deprecated; "
				"use slaptest command instead\n" );
			check |= CHECK_CONFIG;
			break;

		case 'V':
			version++;
			break;

		case 'T':
			if ( firstopt == 0 ) {
				fprintf( stderr, "warning: \"-T %s\" "
					"should be the first option.\n",
					optarg );
			}

#ifdef DEBUG_CLOSE
			extern void slapd_debug_close();
			slapd_debug_close();
#endif
			/* try full option string first */
			for ( i = 0; tools[i].name; i++ ) {
				if ( strcmp( optarg, &tools[i].name[4] ) == 0 ) {
					rc = tools[i].func( argc, argv );
					MAIN_RETURN( rc );
				}
			}

			/* try bits of option string (backward compatibility for single char) */
			l = strlen( optarg );
			for ( i = 0; tools[i].name; i++ ) {
				if ( strncmp( optarg, &tools[i].name[4], l ) == 0 ) {
					rc = tools[i].func( argc, argv );
					MAIN_RETURN( rc );
				}
			}
			
			/* issue error */
			serverName = optarg;
			serverNamePrefix = "slap";
			fprintf( stderr, "program name \"%s%s\" unrecognized; "
					"aborting...\n", serverNamePrefix, serverName );
			/* FALLTHRU */
		default:
unhandled_option:;
			usage( argv[0] );
			rc = 1;
			SERVICE_EXIT( ERROR_SERVICE_SPECIFIC_ERROR, 15 );
			goto stop;
		}

		if ( firstopt ) {
			firstopt = 0;
		}
	}

	if ( optind != argc )
		goto unhandled_option;

	ber_set_option(NULL, LBER_OPT_LOG_PRINT_FN, slap_debug_print);
	ber_set_option(NULL, LBER_OPT_DEBUG_LEVEL, &slap_debug);
	ldap_set_option(NULL, LDAP_OPT_DEBUG_LEVEL, &slap_debug);
	ldif_debug = slap_debug;
	slap_debug_orig = slap_debug;

	if ( version ) {
		fprintf( stderr, "%s\n", Versionstr );
		if ( version > 2 ) {
			if ( slap_oinfo[0].ov_type ) {
				fprintf( stderr, "Included static overlays:\n");
				for ( i= 0 ; slap_oinfo[i].ov_type; i++ ) {
					fprintf( stderr, "    %s\n", slap_oinfo[i].ov_type );
				}
			}
			if ( slap_binfo[0].bi_type ) {
				fprintf( stderr, "Included static backends:\n");
				for ( i= 0 ; slap_binfo[i].bi_type; i++ ) {
					fprintf( stderr, "    %s\n", slap_binfo[i].bi_type );
				}
			}
		}

		if ( version > 1 ) goto stop;
	}

#if defined(LDAP_DEBUG) && defined(LDAP_SYSLOG)
	{
		char *logName;
#ifdef HAVE_EBCDIC
		logName = ch_strdup( serverName );
		__atoe( logName );
#else
		logName = serverName;
#endif

#ifdef LOG_LOCAL4
		openlog( logName, OPENLOG_OPTIONS, syslogUser );
#elif defined LOG_DEBUG
		openlog( logName, OPENLOG_OPTIONS );
#endif
#ifdef HAVE_EBCDIC
		free( logName );
#endif
	}
#endif /* LDAP_DEBUG && LDAP_SYSLOG */

	Debug( LDAP_DEBUG_ANY, "%s", Versionstr );

	if( check == CHECK_NONE && slapd_daemon_init( urls ) != 0 ) {
		rc = 1;
		SERVICE_EXIT( ERROR_SERVICE_SPECIFIC_ERROR, 16 );
		goto stop;
	}

#if defined(HAVE_CHROOT)
	if ( sandbox ) {
		if ( chdir( sandbox ) ) {
			perror("chdir");
			rc = 1;
			goto stop;
		}
		if ( chroot( sandbox ) ) {
			perror("chroot");
			rc = 1;
			goto stop;
		}
		if ( chdir( "/" ) ) {
			perror("chdir");
			rc = 1;
			goto stop;
		}
	}
#endif

#if defined(HAVE_SETUID) && defined(HAVE_SETGID)
	if ( username != NULL || groupname != NULL ) {
		slap_init_user( username, groupname );
	}
#endif

	extops_init();
	lutil_passwd_init();

	rc = slap_init( serverMode, serverName );
	if ( rc ) {
		SERVICE_EXIT( ERROR_SERVICE_SPECIFIC_ERROR, 18 );
		goto destroy;
	}

	if ( read_config( configfile, configdir ) != 0 ) {
		rc = 1;
		SERVICE_EXIT( ERROR_SERVICE_SPECIFIC_ERROR, 19 );

		if ( check & CHECK_CONFIG ) {
			fprintf( stderr, "config check failed\n" );
		}

		goto destroy;
	}

	rc = slap_parse_debug_unknowns();
	if ( rc )
		goto destroy;

	if ( check & CHECK_LOGLEVEL ) {
		rc = 0;
		goto destroy;
	}

	if ( check & CHECK_CONFIG ) {
		fprintf( stderr, "config check succeeded\n" );

		check &= ~CHECK_CONFIG;
		if ( check == CHECK_NONE ) {
			rc = 0;
			goto destroy;
		}
	}

	if ( glue_sub_attach( 0 ) != 0 ) {
		Debug( LDAP_DEBUG_ANY,
		    "subordinate config error\n" );

		goto destroy;
	}

	if ( slap_schema_check( ) != 0 ) {
		Debug( LDAP_DEBUG_ANY,
		    "schema prep error\n" );

		goto destroy;
	}

#ifdef HAVE_TLS
	rc = ldap_pvt_tls_init( 1 );
	if( rc != 0) {
		Debug( LDAP_DEBUG_ANY,
		    "main: TLS init failed: %d\n",
		    rc );
		rc = 1;
		SERVICE_EXIT( ERROR_SERVICE_SPECIFIC_ERROR, 20 );
		goto destroy;
	}

	{
		int opt = 1;

		/* Force new ctx to be created */
		rc = ldap_pvt_tls_set_option( slap_tls_ld, LDAP_OPT_X_TLS_NEWCTX, &opt );
		if( rc == 0 ) {
			/* The ctx's refcount is bumped up here */
			ldap_pvt_tls_get_option( slap_tls_ld, LDAP_OPT_X_TLS_CTX, &slap_tls_ctx );
			load_extop( &slap_EXOP_START_TLS, 0, starttls_extop );
		} else if ( rc != LDAP_NOT_SUPPORTED ) {
			char *errmsg = NULL;
			ldap_get_option( slap_tls_ld, LDAP_OPT_DIAGNOSTIC_MESSAGE, &errmsg );
			Debug( LDAP_DEBUG_ANY,
			    "main: TLS init def ctx failed: %d %s\n",
			    rc, errmsg ? errmsg : "" );
			ldap_memfree( errmsg );
			rc = 1;
			SERVICE_EXIT( ERROR_SERVICE_SPECIFIC_ERROR, 20 );
			goto destroy;
		}
	}
#endif

#ifdef HAVE_CYRUS_SASL
	if( sasl_host == NULL ) {
		sasl_host = ch_strdup( global_host );
	}
#endif

	(void) SIGNAL( LDAP_SIGUSR1, slap_sig_wake );
	(void) SIGNAL( LDAP_SIGUSR2, slap_sig_shutdown );

#ifdef SIGPIPE
	(void) SIGNAL( SIGPIPE, SIG_IGN );
#endif
#ifdef SIGHUP
	(void) SIGNAL( SIGHUP, slap_sig_shutdown );
#endif
	(void) SIGNAL( SIGINT, slap_sig_shutdown );
	(void) SIGNAL( SIGTERM, slap_sig_shutdown );
#ifdef SIGTRAP
	(void) SIGNAL( SIGTRAP, slap_sig_shutdown );
#endif
#ifdef LDAP_SIGCHLD
	(void) SIGNAL( LDAP_SIGCHLD, wait4child );
#endif
#ifdef SIGBREAK
	/* SIGBREAK is generated when Ctrl-Break is pressed. */
	(void) SIGNAL( SIGBREAK, slap_sig_shutdown );
#endif

#ifndef HAVE_WINSOCK
	if ( !no_detach ) {
		if ( lutil_pair( waitfds ) < 0 ) {
			Debug( LDAP_DEBUG_ANY,
				"main: lutil_pair failed: %d\n",
				0 );
			rc = 1;
			goto destroy;
		}
		pid = lutil_detach( no_detach, 0 );
		if ( pid ) {
			char buf[4];
			rc = EXIT_SUCCESS;
			close( waitfds[1] );
			if ( read( waitfds[0], buf, 1 ) != 1 )
				rc = EXIT_FAILURE;
			_exit( rc );
		} else {
			close( waitfds[0] );
		}
	}
#endif /* HAVE_WINSOCK */

#ifdef CSRIMALLOC
	mal_leaktrace(1);
#endif

	if ( slapd_pid_file != NULL ) {
		FILE *fp = fopen( slapd_pid_file, "w" );

		if ( fp == NULL ) {
			char ebuf[128];
			int save_errno = errno;

			Debug( LDAP_DEBUG_ANY, "unable to open pid file "
				"\"%s\": %d (%s)\n",
				slapd_pid_file,
				save_errno, AC_STRERROR_R( save_errno, ebuf, sizeof(ebuf) ) );

			free( slapd_pid_file );
			slapd_pid_file = NULL;

			rc = 1;
			goto destroy;
		}
		fprintf( fp, "%d\n", (int) getpid() );
		fclose( fp );
		slapd_pid_file_unlink = 1;
	}

	if ( slapd_args_file != NULL ) {
		FILE *fp = fopen( slapd_args_file, "w" );

		if ( fp == NULL ) {
			char ebuf[128];
			int save_errno = errno;

			Debug( LDAP_DEBUG_ANY, "unable to open args file "
				"\"%s\": %d (%s)\n",
				slapd_args_file,
				save_errno, AC_STRERROR_R( save_errno, ebuf, sizeof(ebuf) ) );

			free( slapd_args_file );
			slapd_args_file = NULL;

			rc = 1;
			goto destroy;
		}

		for ( i = 0; i < g_argc; i++ ) {
			fprintf( fp, "%s ", g_argv[i] );
		}
		fprintf( fp, "\n" );
		fclose( fp );
		slapd_args_file_unlink = 1;
	}

	/*
	 * FIXME: moved here from slapd_daemon_task()
	 * because back-monitor db_open() needs it
	 */
	time( &starttime );

	connections_init();

	if ( slap_startup( NULL ) != 0 ) {
		rc = 1;
		SERVICE_EXIT( ERROR_SERVICE_SPECIFIC_ERROR, 21 );
		goto shutdown;
	}

	Debug( LDAP_DEBUG_ANY, "slapd starting\n" );

#ifndef HAVE_WINSOCK
	if ( !no_detach ) {
		write( waitfds[1], "1", 1 );
		close( waitfds[1] );
	}
#endif

#ifdef HAVE_NT_EVENT_LOG
	if (is_NT_Service)
	lutil_LogStartedEvent( serverName, slap_debug, configfile ?
		configfile : SLAPD_DEFAULT_CONFIGFILE , urls );
#endif

	rc = slapd_daemon();

#ifdef HAVE_NT_SERVICE_MANAGER
	/* Throw away the event that we used during the startup process. */
	if ( is_NT_Service )
		ldap_pvt_thread_cond_destroy( &started_event );
#endif

shutdown:
	/* remember an error during shutdown */
	rc |= slap_shutdown( NULL );

destroy:
	if ( check & CHECK_LOGLEVEL ) {
		(void)loglevel_print( stdout );
	}
	/* remember an error during destroy */
	rc |= slap_destroy();

	while ( !LDAP_STAILQ_EMPTY( &slap_sync_cookie )) {
		scp = LDAP_STAILQ_FIRST( &slap_sync_cookie );
		LDAP_STAILQ_REMOVE_HEAD( &slap_sync_cookie, sc_next );
		ch_free( scp );
	}

#ifdef SLAPD_MODULES
	module_kill();
#endif

	extops_kill();

	supported_feature_destroy();
	entry_info_destroy();

stop:
#ifdef HAVE_NT_EVENT_LOG
	if (is_NT_Service)
	lutil_LogStoppedEvent( serverName );
#endif

	Debug( LDAP_DEBUG_ANY, "slapd stopped.\n" );


#ifdef HAVE_NT_SERVICE_MANAGER
	lutil_ReportShutdownComplete();
#endif

#ifdef LOG_DEBUG
    closelog();
#endif
	slapd_daemon_destroy();

	controls_destroy();

	filter_destroy();

	schema_destroy();

	lutil_passwd_destroy();

#ifdef HAVE_TLS
	if ( slap_tls_ld ) {
		ldap_pvt_tls_ctx_free( slap_tls_ctx );
		ldap_unbind_ext( slap_tls_ld, NULL, NULL );
	}
	ldap_pvt_tls_destroy();
#endif

	slap_sasl_regexp_destroy();

	if ( slapd_pid_file_unlink ) {
		unlink( slapd_pid_file );
	}
	if ( slapd_args_file_unlink ) {
		unlink( slapd_args_file );
	}

	config_destroy();

	if ( global_host )
		ch_free( global_host );

	/* kludge, get symbols referenced */
	ldap_tavl_free( NULL, NULL );

#ifdef CSRIMALLOC
	mal_dumpleaktrace( leakfile );
#endif

	ldap_pvt_thread_mutex_destroy( &logfile_mutex );
	MAIN_RETURN(rc);
}


#ifdef LDAP_SIGCHLD

/*
 *  Catch and discard terminated child processes, to avoid zombies.
 */

static RETSIGTYPE
wait4child( int sig )
{
    int save_errno = errno;

#ifdef WNOHANG
    do
        errno = 0;
#ifdef HAVE_WAITPID
    while ( waitpid( (pid_t)-1, NULL, WNOHANG ) > 0 || errno == EINTR );
#else
    while ( wait3( NULL, WNOHANG, NULL ) > 0 || errno == EINTR );
#endif
#else
    (void) wait( NULL );
#endif
    (void) SIGNAL_REINSTALL( sig, wait4child );
    errno = save_errno;
}

#endif /* LDAP_SIGCHLD */
