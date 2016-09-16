//-------------------------------------------------------------------------------------------------------
// Copyright (C) 2016 HHD Software Ltd.
// Written by Alexander Bessonov
//
// Licensed under the MIT license. See LICENSE.txt file in the project root for full license information.
//-------------------------------------------------------------------------------------------------------

#include <string>
#include <iostream>

#include <chakra_bridge/chakra_bridge.h>
#pragma comment(lib,"ChakraCore")

void main()
{
	using namespace std::string_literals;

	jsc::runtime runtime;
	jsc::context ctx;
	try
	{
		// Create a runtime
		check(runtime.create(JsRuntimeAttributeNone));

		// Create a context
		check(ctx.create(runtime));

		// Make this a current context for this scope
		jsc::scoped_context sc(ctx);

		// JavaScript code
		auto script_body = LR"==(
function sum(arg1, arg2) {
	return arg1 + arg2;
}

function testExternalFunction() {
	external_function("string value", true, { a: 20, b: [ "a1", null, undefined ] });
}

function testExternalObject(obj) {
	obj.print("a: " + obj.a);
	obj.print("b: " + obj.b);
	obj.print("c: " + obj.c);

	// re-assign property (will cause property put accessor to be called)
	obj.c = 2;
	obj.print("c: " + obj.c);
}
)=="s;

		// Execute script
		auto result = jsc::RunScript(script_body.c_str(), 0, L"");

		// 1. Run JavaScript function sum to add two integer values
		std::wcout << jsc::value::global()[L"sum"](nullptr, 2, 3).as_int() << L"\r\n"s;	// prints 5

		// 2. Add an external function to a script
		jsc::value::global()[L"external_function"] =
			jsc::value::function<3>([](const std::wstring &sval, bool bval, jsc::value object)
		{
			std::wcout <<
				L"String argument: "s << sval <<
				L"\r\nBoolean argument: "s << bval <<
				L"\r\nInteger argument in object: "s << static_cast<int>(object[L"a"]) <<
				L"\r\nLength of JavaScript array: "s << object[L"b"][L"length"].as<int>() << L"\r\n"s;

			// Return value to JavaScript
			return true;
		});

		// 3. Run JavaScript function
		jsc::value::global()[L"testExternalFunction"](nullptr);	// will make a lambda above called

		// 4. Create a JavaScript object
		int c = 42;
		auto obj = jsc::value::object()
			.field(L"a", 10)	// constant property value
			.property(L"b", [] { return L"Read-only property"; })
			.property(L"c", [&] { return c; }, [&](int new_c) { c = new_c; })
			.method<1>(L"print", [](const std::wstring &message)
		{
			std::wcout << message << L"\r\n";
		});

		// 5. Run JavaScript function
		jsc::value::global()[L"testExternalObject"](nullptr, obj);
	}
	catch (const jsc::exception &e)
	{
		std::wcout << L"Exception code: "s << e.code() << L"\r\n"s;
	}
}
