#ifndef ALEXCPT_H
#define ALEXCPT_H

#include <exception>


#define START_API_FUNC try

#define END_API_FUNC catch(...) { std::terminate(); }

#endif /* ALEXCPT_H */
