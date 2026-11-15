module;

#include <fstream>
#include <ios>
#include <iostream>
#include <istream>
#include <ostream>
#include <sstream>
#include <streambuf>

export module common.std;

/* NOLINTBEGIN(bugprone-std-namespace-modification, cert-dcl58-cpp) */
export namespace std {

    using std::cerr;
    using std::cin;
    using std::clog;
    using std::cout;

	using std::hex;
	using std::dec;
	using std::oct;
	using std::fixed;
	using std::flush;
    using std::endl;

	using std::ios_base;
	using std::basic_ios;
	using std::basic_streambuf;
	using std::basic_ostream;
	using std::basic_istream;
	using std::basic_iostream;

	using std::basic_filebuf;
	using std::basic_ofstream;
	using std::basic_ifstream;
	using std::basic_fstream;

	using std::basic_stringbuf;
	using std::basic_ostringstream;
	using std::basic_istringstream;
	using std::basic_stringstream;

	using std::streamoff;
	using std::streamsize;
	using std::fpos;
    using std::streampos;

	using std::ios;
	using std::streambuf;
	using std::istream;
	using std::ostream;
	using std::iostream;

	using std::filebuf;
	using std::ofstream;
	using std::ifstream;
	using std::fstream;

	using std::stringbuf;
	using std::ostringstream;
	using std::istringstream;
	using std::stringstream;

	using std::io_errc;
	using std::iostream_category;

	using std::operator>>;
	using std::operator<<;

}
/* NOLINTEND(bugprone-std-namespace-modification, cert-dcl58-cpp) */
