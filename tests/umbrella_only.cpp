/* Deliberately one line. If <stripseq/stripseq.h> ever stops compiling on its
 * own -- a header that uses uint16_t without including <cstdint>, or one that
 * only works because something else was included first -- this translation
 * unit is what says so, in every build variant.
 *
 * Do not add anything to this file. Anything else it includes weakens it.
 */

#include <stripseq/stripseq.h>
