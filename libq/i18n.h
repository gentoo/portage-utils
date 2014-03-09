#ifndef _I18N_H
#define _I18N_H

#if ENABLE_NLS
# include <locale.h>
# include <libintl.h>
# define _(String) gettext (String)
# define decimal_point localeconv()->decimal_point
#else
# define _(String) (String)
# define setlocale(x,y)
# define bindtextdomain(x,y)
# define textdomain(x)
# define decimal_point "."
#endif

#endif
