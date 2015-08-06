#ifndef HEADERS_FIELDS_HPP
#define HEADERS_FIELDS_HPP

#include <string>

#define HEADER(FOO) \
	FOO(ip_src) \
	FOO(ip_dst) \
	FOO(ip_chksum) \
	FOO(ip_TTL) \
	FOO(tcp_srcPort) \
	FOO(tcp_dstPort)


#define DO_DESCRIPTION(e) #e,
#define DO_ENUM(e) e,

const std::string headerFieldNames[8] = {
HEADER(DO_DESCRIPTION)
};

enum HeaderField {
HEADER(DO_ENUM)
};

#endif
