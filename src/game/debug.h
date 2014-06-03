#ifndef DEBUG_H
#define DEBUG_H


#ifdef QT_DEBUG
#define DEBUG_MSG(str) do { /*cout<< str << std::endl;*/ } while( false )
#else
#define DEBUG_MSG(str) do { /*cout<<str << std::endl;*/ } while( false )
#endif

#endif // DEBUG_H
