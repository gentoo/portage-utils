
/* color constants */
#ifdef OPTIMIZE_FOR_SIZE
# define _MAKE_COLOR(c,b) ""
#else
# define _MAKE_COLOR(c,b) "\e[" c ";" b "m"
#endif
static const char *BOLD = _MAKE_COLOR("00", "01");
static const char *NORM = _MAKE_COLOR("00", "00");
static const char *BLUE = _MAKE_COLOR("36", "01");
static const char *DKBLUE = _MAKE_COLOR("34", "01");
static const char *CYAN = _MAKE_COLOR("00", "36");
static const char *GREEN = _MAKE_COLOR("32", "01");
static const char *DKGREEN = _MAKE_COLOR("00", "32");
static const char *MAGENTA = _MAKE_COLOR("00", "35");
static const char *RED = _MAKE_COLOR("31", "01");
static const char *YELLOW = _MAKE_COLOR("33", "01");
static const char *WHITE = _MAKE_COLOR("01", "38");

