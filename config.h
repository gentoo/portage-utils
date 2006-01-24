/* control which applets to include
 *
 * #undef OMIT_QFOO  -> include QFOO
 * #define OMIT_QBAR -> disable QBAR
 */

#undef OMIT_QATOM
#undef OMIT_QCHECK
#undef OMIT_QDEPENDS
#undef OMIT_QFILE
#define OMIT_QGLSA
#undef OMIT_QGREP
#undef OMIT_QLIST
#undef OMIT_QLOP
#define OMIT_QMERGE
#undef OMIT_QPKG
#undef OMIT_QSEARCH
#undef OMIT_QSIZE
#undef OMIT_QTBZ2
#undef OMIT_QUSE
#undef OMIT_QXPAK
