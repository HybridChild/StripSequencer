/* Probe for stripseq_check_config_range() in cmake/stripseq_compile_fail.cmake:
 * one translation unit, compiled once per case with one capacity defined on
 * the command line. config.h must refuse a value outside 1..255 and accept
 * both ends of the range.
 *
 * Not part of any target. It is compiled only by try_compile, which reports
 * whether it compiled and what the compiler said.
 */

#include <stripseq/config.h>
