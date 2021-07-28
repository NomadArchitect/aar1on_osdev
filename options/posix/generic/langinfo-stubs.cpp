
#include <langinfo.h>
#include <bits/ensure.h>
#include <mlibc/debug.hpp>

char *nl_langinfo(nl_item item) {
	if(item == CODESET) {
		return "UTF-8";
	}else{
		mlibc::infoLogger() << "mlibc: nl_langinfo item "
				<< item << " is not implemented properly" << frg::endlog;
		return "";
	}
}

char *nl_langinfo_1(nl_item, locale_t) {
	__ensure(!"Not implemented");
	__builtin_unreachable();
}

