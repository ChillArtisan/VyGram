#include "ayu/security/password_policy.h"

#include <cassert>
#include <iostream>
#include <string>

int main() {
	using Ayu::Security::IsStrongLocalPassword;
	for (const auto password : {
		U"", U"short!1A", U"eleven-chrs", U"            ",
		U"aaaaaaaaaaaa", U"abcdabcdabcd", U"AbCdEfAbCdEf",
		U"123456789012", U"210987654321", U"abcdefghijkl",
		U"lkjihgfedcba", U"qwertyuiopas", U"password1234",
		U"PASSWORD123456", U"qwerty123456789" }) {
		assert(!IsStrongLocalPassword(password));
	}
	for (const auto password : {
		U"twelvechars!", U"violet river stone", U"my coffee cools slowly",
		U"Kx7#fQ2!aL9$zP", U"\u0434\u043e\u043c \u0443 \u0442\u0438\u0445\u043e\u0439 \u0440\u0435\u043a\u0438",
		U"\U0001F680 orbit \U0001F30D station" }) {
		assert(IsStrongLocalPassword(password));
	}
	assert(!IsStrongLocalPassword(U"\U0001F680orbit erth"));
	assert(IsStrongLocalPassword(U"\U0001F680orbit earth"));
	auto invalid = std::u32string(U"violet river stone");
	invalid[1] = 0xD800;
	assert(!IsStrongLocalPassword(invalid));
	invalid[1] = 0xDC00;
	assert(!IsStrongLocalPassword(invalid));
	invalid[1] = 0x110000;
	assert(!IsStrongLocalPassword(invalid));
	assert(!IsStrongLocalPassword(U"\u00A0\u2000\u2001\u2002\u2003\u2004\u2005\u2006\u2007\u2008\u2009\u3000"));
	std::cout << "Local password policy: all assertions passed.\n";
}