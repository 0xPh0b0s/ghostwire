/*
 * ghostwire.h — shared definitions
 * Author: 0xNullVector
 */

#ifndef _GHOSTWIRE_H
#define _GHOSTWIRE_H

/* /proc control interface name */
#define GHOSTWIRE_PROC_NAME     "ghostwire"

/* Default file/dir hiding prefix — anything starting with ".gw_" is invisible */
#define GHOSTWIRE_HIDDEN_PREFIX ".gw_"

/* Magic signal for PID existence spoofing */
#define GHOSTWIRE_MAGIC_SIG     31

/* Version string */
#define GHOSTWIRE_VERSION       "1.0.0"

#endif /* _GHOSTWIRE_H */
