#ifndef ELOG_H
#define ELOG_H

void elog_warn(char const* tag, char const* format, ...);
void elog_error(char const* tag, char const* format, ...);

#endif
