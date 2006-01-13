#ifndef I18N_H
#define I18N_H

#ifdef ENABLE_NLS
# include <locale.h>
# include <libintl.h>
# define _(String) gettext (String)
#else
# define _(String) (String)
#endif

#endif
