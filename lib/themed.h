#include <string>
#include <cpptk.h>

extern std::string lib_themed;

decltype(Tk::frame({})) gold_frame(const std::string& w);
decltype(Tk::label({})) tlabel(const std::string& w);
decltype(Tk::text({})) ttext(std::string w);
decltype(Tk::frame({})) textframe(const std::string& w);
