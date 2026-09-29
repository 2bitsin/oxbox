// The public headers after <windows.h>, no NOMINMAX; asio needs <winsock2.h> first.
#include <winsock2.h>  // or <windows.h> brings winsock.h, which asio refuses (C1189)
#include <windows.h>

#include "oxbox/http/unit.test/headers.cpp"
