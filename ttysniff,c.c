#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/select.h>
#include <ctype.h>
#include <getopt.h>

#define VERSION "2.6.0"

/* values for parity */
#define SERIAL_NO_PARITY    1
#define SERIAL_EVEN_PARITY  2
#define SERIAL_ODD_PARITY   4

/* values for data bits */
#define SERIAL_8_DATA_BITS  8 
#define SERIAL_7_DATA_BITS  7 

/* values for stop bits */
#define SERIAL_1_STOP_BITS  1 
#define SERIAL_2_STOP_BITS  2 

/* flow control */
#define FLOW_CONTROL_NONE     1
#define FLOW_CONTROL_HARDWARE 2
#define FLOW_CONTROL_SOFTWARE 3

#define FALSE 0
#define TRUE  1

#define DEFAULT_BAUD_RATE  B9600
//#define BAUD_RATE  B19200
//#define BAUD_RATE  B38400
            
#define NON_BLOCKING_SERIAL 

char serial_port[FILENAME_MAX+1];
char logfile_name[FILENAME_MAX+1];

/* log output to file */
int opt_log_output = FALSE; 

/* dump output as hex */
int opt_hex_output = TRUE;  

/* strip high bit from all chars sent to stdout */ 
int opt_strip_high_bit = FALSE;  

/* default is to transmit CRLF on CR from stdin; when true, only the CR is
 * transmitted to remote 
 */
int opt_bare_cr = FALSE;  

speed_t baudrate = DEFAULT_BAUD_RATE;

/* at some point, could make these command line configurable */
int parity = SERIAL_NO_PARITY;
//int databits = SERIAL_7_DATA_BITS;
int databits = SERIAL_8_DATA_BITS;
int stopbits = SERIAL_1_STOP_BITS;
//int flow_control = FLOW_CONTROL_SOFTWARE;
//int flow_control = FLOW_CONTROL_HARDWARE;
int flow_control = FLOW_CONTROL_NONE;

int main_quit = 0;

typedef struct {
    const char *str;
    int len; /* string length of str */
    speed_t speed;
} BaudRate;

BaudRate baudrates[] = {
    { "110",  3, B110   },
    { "150",  3, B150   },
    { "300",  3, B300   },
    { "600",  3, B600   },
    { "1200", 4, B1200  },
    { "2400", 4, B2400  },
    { "4800", 4, B4800  },
    { "9600", 4, B9600  },
    { "19200",  5, B19200 },
    { "38400",  5, B38400 },
    { "57600",  5, B57600 },
    { "115200", 6, B115200},
    { "230400", 6, B230400},
};

const unsigned char CR = '\r';
const unsigned char LF = '\n';

#if defined linux || defined unix || defined __GNUC__
void signal_term( int signum )
{
    fprintf( stderr, "signal!\n" );
    main_quit = 1;
}

void init_signals( void )
{
    int uerr;
    struct sigaction sigterm; 
    struct sigaction sigint; 

    memset( &sigterm, 0, sizeof(sigterm) );
    sigterm.sa_handler = signal_term;
    uerr = sigaction( SIGTERM, &sigterm, NULL );
    if( uerr != 0 ) {
        perror( "sigaction(SIGTERM)" );
        exit(1);
    }

    memset( &sigint, 0, sizeof(sigint) );
    sigint.sa_handler = signal_term;
    uerr = sigaction( SIGINT, &sigint, NULL );
    if( uerr != 0 ) {
        perror( "sigaction(SIGINT)" );
        exit(1);
    }
}
#endif

int serial_open_port( void )
{
    struct termios oldtio, newtio;
    int fd;

    /* stupid human check -- I've been running this thing 
     *  as root (very very dumb) so I don't want to accidently
     *  write DNP to /dev/hda1 or something equally tragic.
     */
//    if( strncmp( path, "/dev/ttyS", 9 ) != 0 ) {
//        LOG_MESSAGE1( LOG_ERR, "\"%s\" not a serial port.", path );        
//        return ERR_FAIL;
//    }

    /* davep 29-Apr-2009 ; temp debug */
    fprintf( stderr, "%s opening %s\n", __FUNCTION__, serial_port );

    /* open serial port */
#ifdef NON_BLOCKING_SERIAL
    fd = open( serial_port, O_RDWR|O_NOCTTY|O_NONBLOCK );
#else
    fd = open( serial_port, O_RDWR|O_NOCTTY );
#endif
    if( fd < 0 ) {
        fprintf( stderr, "open() of %s failed : %s", 
                serial_port, strerror(errno) );
        return -1;
    }

    /* davep 29-Apr-2009 ; temp debug */
    fprintf( stderr, "%s opened %s successfully\n", __FUNCTION__, serial_port );

    tcgetattr( fd, &oldtio );

    memset( &newtio, 0, sizeof(newtio) );

    newtio.c_cflag = CLOCAL | CREAD ;

    /* parity */
    switch( parity ) {
        case SERIAL_NO_PARITY :
            /* nothing -- default is no parity */
            break;

        case SERIAL_EVEN_PARITY :
            newtio.c_cflag |= PARENB;
            break;

        case SERIAL_ODD_PARITY :
            newtio.c_cflag |= PARENB | PARODD;
            break;

        default :
            assert( 0 );
    }

    /* data bits */
    if( databits == SERIAL_8_DATA_BITS ) {
        newtio.c_cflag |= CS8;
    }
    else if( databits == SERIAL_7_DATA_BITS ) {
        newtio.c_cflag |= CS7;
    }
    else {
        assert( 0 );
    }

    /* stop bits */
    if( stopbits == SERIAL_1_STOP_BITS ) {
        /* nothing -- default is 1 stop bit */
    }
    else if( stopbits == SERIAL_2_STOP_BITS ) {
        newtio.c_cflag |= CSTOPB;
    }
    else {
        assert( 0 );
    }

    /* hardware flow control? */
    if( flow_control==FLOW_CONTROL_HARDWARE ) {
        newtio.c_cflag |= CRTSCTS;
    }

    newtio.c_lflag = 0;
    /* no additional lflags */

    newtio.c_iflag = 0; // | IXON | IXANY | IXOFF | IMAXBEL;

    /* software flow control? */
    if( flow_control==FLOW_CONTROL_SOFTWARE ) {
        newtio.c_iflag |= (IXON | IXOFF | IXANY);
    }

    newtio.c_oflag = 0;
    /* no additional oflags */

    cfsetospeed( &newtio, baudrate );
    cfsetispeed( &newtio, baudrate );

#ifndef NON_BLOCKING_SERIAL
    newtio.c_cc[VMIN]  = 1; /* block until this many chars recvd */
    newtio.c_cc[VTIME] = 0; /* inter character timer unused */
#endif

    tcflush( fd, TCIFLUSH );
    tcsetattr( fd, TCSANOW, &newtio );

    return fd;
}

int baud_string_to_baud( char *baudstring, speed_t *baudrate )
{
    int i;

    for( i=0 ; i<sizeof(baudrates)/sizeof(BaudRate) ; i++ ) {
        if(strncasecmp(baudrates[i].str,baudstring,baudrates[i].len )==0) {
            /* found it */
            *baudrate = baudrates[i].speed;
            return 0;
        }
    }

    return -1;
}

const char *baud_to_baud_string( speed_t baudrate )
{
    int i;

    for( i=0 ; i<sizeof(baudrates)/sizeof(BaudRate) ; i++ ) {
        if( baudrates[i].speed == baudrate ) {
            /* found it */
            return baudrates[i].str;
        }
    }

    return NULL;
}

void print_usage( void )
{
    int i;

    printf( "ttysniff %s (%s) - read and dump data from serial port.\n", 
            VERSION, __DATE__ );
    printf( "usage: ttysniff [-options] [-l logfile] [-b baud] [-f flowcontrol] path\n" );
    printf( "  -h                   show this help\n" );
    printf( "  -p                   print data as readable (default is to print as hex)\n" );
    printf( "  -l logfile           log traffic to binary file\n" );
    printf( "  -b baudrate          set baud rate (default=%s)\n", 
            baud_to_baud_string(DEFAULT_BAUD_RATE) );
    printf( "  -7                   7 data bits\n" );
    printf( "  -8                   8 data bits (default)\n" );
    printf( "  -f [hard|soft]       enable hardware (RTS/CTS) or software (XON/XOFF) flow control\n" );
    printf( "                       (default is no flow control)\n" );
    printf( "  --strip-high-bit     remove high bit (1<<8) from chars before printing\n" );
    printf( "                       (doesn't effect logging to file or printing as hex)\n" );
    printf( "  --bare-cr            don't send CRLF when reading CR from stdin\n" );
    printf( "                       (default is to send CRLF when reading CR on input)\n" );
    printf( "  path                 path to serial port (e.g., /dev/ttyS0)\n" );
    printf( "\n" );
    printf( "Available baud rates are: " );

    for( i=0 ; i<sizeof(baudrates)/sizeof(BaudRate) ; i++ ) {
        printf( "%s ", baudrates[i].str );
    }
    printf( "\n" );
}

static int str_match( const char *s1, const char *s2, size_t maxlen )
{
    /* Strings must exactly match and must be exactly maxlen. The maxlen+1
     * finds strings longer than maxlen
     */
    if( strnlen(s1,maxlen+1)<=maxlen &&
        strnlen(s2,maxlen+1)<=maxlen &&
        strncmp(s1,s2,maxlen)==0 ){
        return TRUE;
    }
    return FALSE;
}

static int parse_long_option( const char *long_opt_name, 
                              const char *long_opt_value )
{
    if( str_match( long_opt_name, "strip-high-bit", 14 ) ) {
        opt_strip_high_bit = TRUE;
    }
    else if( str_match( long_opt_name, "bare-cr", 8 ) ) {
        /* don't send a LF to remote side when read CR from stdin */
        opt_bare_cr = TRUE;
    }

    return 0;
}

int parse_args( int argc, char *argv[] )
{
    int retcode;
    char c;
    static struct option long_options[] = {
        { "strip-high-bit", 0, 0, 0 },   /* strip high bit (c&0x7f) */
        { "bare-cr", 0, 0, 0 }, /* don't send extra LF on reading CR from stdin */
        { 0, 0, 0, 0 },
    };
    int long_index;

    if( argc == 1 ) {
        print_usage();
        exit(1);
    }

    baudrate = DEFAULT_BAUD_RATE;

    while( 1 ) {
        c = getopt_long( argc, argv, "hpl:b:78f:", long_options, &long_index );

        if( c==-1 )
            break;

        switch( c ) {
            case 0 : 
                /* handle long option */
                retcode = parse_long_option( long_options[long_index].name, optarg );
                if( retcode != 0 ) {
                    exit(1);
                }
                break;

            case 'h' :
                print_usage();
                exit(1);
                break;

            case 'l' :
                /* save output file name */
                strncpy( logfile_name, optarg, FILENAME_MAX );
                opt_log_output = TRUE;
                break;

            case 'p' :
                opt_hex_output = FALSE;
                break;

            case 'b' :
                /* convert baud rate string to baud rate number */
                if( baud_string_to_baud( optarg, &baudrate ) != 0 ) {
                    fprintf( stderr, "Invalid baud rate \"%s\"\n", optarg );
                    exit(1);
                }
                break;

            case '7' :
                /* 7 data bits */
                databits = SERIAL_7_DATA_BITS;
                break;

            case '8' :
                /* 8 data bits */
                databits = SERIAL_8_DATA_BITS;
                break;

            case 'f' :
                if( strncmp( optarg, "hard", 4 ) == 0 ) {
                    flow_control = FLOW_CONTROL_HARDWARE;
                }
                else if( strncmp( optarg, "soft", 4 ) == 0 ) {
                    flow_control = FLOW_CONTROL_SOFTWARE;
                }
                else {
                    fprintf( stderr, "Invalid flow control \"%s\"\n", optarg );
                    exit(1);
                }
                break;

            case '?' :
                print_usage();
                exit(1);
                break;

            default :
                break;
        }
    }

    
    /* get serial port name */
    if (optind < argc) {
        strncpy( serial_port, argv[optind++], FILENAME_MAX );
    }
    else {
        fprintf( stderr, "You must specify a serial port.\n" );
        print_usage();
        exit(1);
    }

    /* XXX temp debug */
//    fprintf( stderr, "baud=%ld port=%s\n", baudrate, serial_port );

    return 0;
}

int
terminal_to_raw( struct termios *save_tios )
{
    struct termios newtios;
    int stdin_fileno;
    int retcode;

    stdin_fileno = fileno(stdin);

    /* get a copy well save until we restore stdin at exit */
    retcode = tcgetattr( stdin_fileno, save_tios );
    if( retcode < 0 ) {
        fprintf( stderr, "tcgetattr() failed to get stdin term attributes: %d %s\n", 
                errno, strerror(errno));
        return -1;
    }

    /* get them again so we can muck with them */
    retcode = tcgetattr( stdin_fileno, &newtios );
    if( retcode < 0 ) {
        fprintf( stderr, "tcgetattr() failed to get stdin term attributes: %d %s\n", 
                errno, strerror(errno));
        return -1;
    }

    cfmakeraw( &newtios );

    /* set stdin to raw */
    retcode = tcsetattr( stdin_fileno, TCSANOW, &newtios );
    if( retcode < 0 ) {
        fprintf( stderr, "tcsetattr() failed to set stdin term attributes: %d %s\n", 
                errno, strerror(errno));
        return -1;
    }

    return 0;
}

int main( int argc, char *argv[] )
{
    int err;
    FILE *binfile=NULL;
    int stdin_fileno, fd;
    unsigned char databyte;
    int count;
    fd_set read_fds;
    int max_fd;
    struct termios stdin_tios;
    
    parse_args( argc, argv );

    init_signals();

    if( opt_log_output ) {
        binfile = fopen( logfile_name, "a" );
        if( binfile == NULL ) {
            fprintf( stderr, "Unable to open log file \"%s\" : %s\n", 
                    logfile_name, strerror(errno) );
            exit(1);
        }
        setbuf( binfile, NULL );
    }

    fd = serial_open_port();
    if( fd < 0 ) {
        /* serial_open_port() displays error */
        exit(1);
    }

    /* set the terminal to raw so we can get single keystrokes */
    fprintf( stderr, "setting stdin to raw...\n" );
    memset( &stdin_tios, 0, sizeof(struct termios) );
    terminal_to_raw( &stdin_tios );

    /* davep 20-July-06 ; XXX TEMPORARY FOR DEBUGGING kick off some activity */
//    write( fd, "scan var print\r\n", 16 );

    setbuf( stdout, NULL );
    count = 0;
    stdin_fileno = fileno(stdin);
    while( !main_quit ) {

//        fprintf( stderr, "stdin=%d\n", fileno(stdin));

        FD_ZERO( &read_fds );
        FD_SET( stdin_fileno, &read_fds );
        FD_SET( fd, &read_fds );

        max_fd = stdin_fileno;
        if( fd > max_fd ) {
            max_fd = fd;
        }

        err = select( max_fd+1, &read_fds, NULL, NULL, NULL );
        if( err<0 ) {
            fprintf( stderr, "select err=%d errno=%d (%s)\r\n", err, errno,
                    strerror(errno) );
            continue;
        }
        
        if( FD_ISSET( stdin_fileno, &read_fds ) ) {
            err = read( stdin_fileno, &databyte, 1 );
            if( err <= 0 ) {
                fprintf( stderr, "read stdin err=%d errno=%d (%s)\r\n", 
                        err, errno, strerror(errno) );
                break;
            }

            if( databyte == 0x03 ) {
                /* Ctrl-C? */
                break;
            }

            write( fd, &databyte, 1 );
            if( databyte==CR && !opt_bare_cr ) {
                write( fd, &LF, 1 );
            }
        }

        if( FD_ISSET( fd, &read_fds ) ) {
            err = read( fd, &databyte, 1 );
            if( err <= 0 ) {
                fprintf( stderr, "read fd err=%d errno=%d (%s)\r\n", 
                        err, errno, strerror(errno) );
                break;
            }

            if( opt_hex_output ) {
                /* There is something weird in printf().  I want to see 0x00 and it
                 * prints "0" instead.  Messes up my pretty screen alignment.
                 */
                if( databyte ) {
#ifdef PRINT_0X
                    printf( "%#04x ", databyte );
                }
                else {
                    printf( "0x00 " );
                }
#else
                    printf( "%02x ", databyte );
                }
                else {
                    printf( "00 " );
                }
#endif
            }
            else {
                /* buyer beware -- just print whatever we get */

                /* added some simple "filtering"; TODO add regex filtering so
                 * really annoying stuff never hits human eyeballs
                 */
                int printit = 1;

                /* davep 10-May-2012 ; bell pissing me off */
                if( databyte==0x07 ) {
                    printit = 0;
                }

                /* davep 19-aug-2011 ; found a platform that uses \n\r not \r\n
                 * (buh?) so filter out the 0x0d 
                 */
                if( databyte==0x0a ) {
                    printf( "%c", 0x0d );
                }
                if( databyte==0x0d ) {
                    printit = 0;
                }

                /* davep 27-Oct-2011 ; quiet down people doing obnoxious stuff like:
                 *
                 *  ******** color_pipe_set_config ENTRY config: 0x5fae64
                 *
                 */
//                if( databyte == '*' ) {
//                    printit = 0;
//                }

                /* davep 01-Aug-2010 ; I did the 0x7f a while ago (strips the
                 * 8th bit) because our newer hardware has issues and the 8th
                 * bit flips up and down randomly.
                 */
                if( printit ) {
                    if( opt_strip_high_bit ) {
                        printf( "%c", databyte&0x7f );
                    }
                    else {
                        printf( "%c", databyte );
                    }
                }

            }

            if( opt_log_output ) {
                fwrite( &databyte, 1, 1, binfile );
                fflush( binfile );
            }
        }

        count++;
    }

    close( fd );
    
    if( opt_log_output ) {
        fclose( binfile );
    }

    /* set stdin back to starting point */  
    fprintf( stderr, "restoring stdin to previous state\r\n" );
    err = tcsetattr( stdin_fileno, TCSANOW, &stdin_tios );
    if( err < 0 ) {
        fprintf( stderr, "tcsetattr() failed to set stdin term attributes: %d %s\n", 
                errno, strerror(errno));
        /* keep going and hope for the best */
    }

    printf( "\nbye!\n" );
    return 0;
}