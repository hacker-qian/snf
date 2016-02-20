//============================================================================
//        Name: chameleon.cpp
//   Copyright: KTH ICT CoS Network Systems Lab
// Description: Class that covers several types of primitives
//============================================================================

#include "chameleon.hpp"
#include "../shared/helpers.hpp"

Chameleon::Chameleon(int i)                    : value_(std::to_string(i)) {}

Chameleon::Chameleon(unsigned short u)         : value_(std::to_string(u)) {}

Chameleon::Chameleon(bool b)                   : value_(std::to_string(b)) {}

Chameleon::Chameleon(double d)                 : value_(std::to_string(d)) {}

Chameleon::Chameleon(const char* c)            : value_(c){}

Chameleon::Chameleon(std::string const& value) : value_(value) {}

Chameleon::Chameleon(Chameleon const& other)   : value_(other.value_) {}

Chameleon&
Chameleon::operator=(int i) {
	value_ = std::to_string(i);
	return *this;
}

Chameleon&
Chameleon::operator=(unsigned short u) {
	value_ = std::to_string(u);
	return *this;
}

Chameleon&
Chameleon::operator=(bool b) {
	value_ = std::to_string(b);
	return *this;
}

Chameleon&
Chameleon::operator=(double i) {
	value_ = std::to_string(i);
	return *this;
}

Chameleon&
Chameleon::operator=(std::string const& s) {
	value_ = s;
	return *this;
}

Chameleon&
Chameleon::operator=(Chameleon const& other) {
	value_ = other.value_;
	return *this;
}

Chameleon::operator int() const {
	return atoi(value_.c_str());
}

Chameleon::operator unsigned short() const {
	return static_cast<unsigned short> ( atoi(value_.c_str()) );
}

Chameleon::operator bool() const {
	return str_to_bool(value_);
}

Chameleon::operator double() const {
	return atof(value_.c_str());
}

Chameleon::operator std::string() const {
	return value_;
}