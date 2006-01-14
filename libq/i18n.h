#ifndef _I18N_H
#define _I18N_H

#ifdef ENABLE_NLS
# include <locale.h>
# include <libintl.h>
# define _(String) gettext (String)
#else
# define _(String) (String)
# define setlocale(x,y)
# define bindtextdomain(x,y)
# define textdomain(x)
#endif

#endif
