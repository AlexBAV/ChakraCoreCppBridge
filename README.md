# Overview of ChakraCoreCppBridge Library

The purpose of this single-header library is to provide a convenient bridge between [ChakraCore] C-style API and C++.

[ChakraCore]: https://github.com/Microsoft/ChakraCore

To start using this library, clone or download it and then `#include` the single header "chakra_bridge/chakra_bridge.h". Library has no external dependencies, except, obviously, for the ChakraCore itself.

## Code Structure

This repository consists of the following subdirectories:

* **include**
  * Contains `chakra_bridge.h` header as well as `chakra_macros.h` header with additional macros for advanced usage scenarios.
* **example**
  * Contains an example project that illustrates the library usage.
* **ChakraCore**
  * Contains a copy of ChakraCore include files and binary files (binary files are not included, see README.md file for instructions on getting them). This directory is only required to build an example project.

## Compiler Support

The library has been tested on Microsoft Visual Studio 2015 Update 3.

## Documentation

ChakraCoreCppBridge defines all its identifiers in `jsc` namespace. It also brings a few helper functions into the global namespace. If you do not want to bring them into global namespace, make sure `CBRIDGE_NO_GLOBAL_NAMESPACE` preprocessor constant is defined before including the library header.

### `runtime` class

`runtime` class is a RAII-style wrapper for `JsRuntimeHandle`. To use it, construct the object of this class and call its `create` method:

```C++
jsc::runtime runtime;
check(runtime.create(JsRuntimeAttributeNone));
```

When `runtime` object goes out of scope, `JsDisposeRuntime` function is called to dispose the ChakraCore runtime.

### `context` class

`context` class is a RAII-style wrapper for `JsContextHandle`. To use it, construct the object of this class and call its `create` method, passing a reference to an initialized `runtime` object:

```C++
jsc::runtime runtime;
jsc::context context;
check(runtime.create(JsRuntimeAttributeNone));
check(context.create(runtime));
```

After the context is successfully created, it may be set as current context with a help of another RAII-style class `scoped_context`:

```C++
{
    jsc::scoped_context sc(context);
    // context is set as current context until the end of scope    
}
```

Remember that unless the current context is set, all other ChakraCore functions will return error code!

### `value` class

This is a "heart" of the ChakraCoreCppBridge library. It is essentially a RAII-style wrapper for `JsValueRef` type with a number of useful methods and operators.

First, let us note that the `value` class is a thin wrapper which has the same size as `JsValueRef`, that is, a size of a pointer. They are cheap to copy, pass and store.

Remember that values of class `value` are never to be stored anywhere except as in a local variable inside the current scope! ChakraCore manages the lifetime of these objects. If you need to save the value outside of the current scope, use the `referenced_value` class instead.

#### Well-known Constants

There are several static methods which should be called to get a reference to built-in or well-known ChakraCore objects and values:

```C++
static value value::null()  // returns a reference to a `Null` object.
static value value::undefined() // returns a reference to an `undefined` value
static value value::true_()  // returns a reference to `true` boolean value
static value value::false_()    // returns a reference to `false` boolean value
```

#### Creating ChakraCore Immediate Values

Immediate values are integer values, floating-point values, boolean values and strings. Class `value` has implicit constructors that take values of different C++ types and convert them to JavaScript types. The following table shows the conversion rules:

| C++ type | JavaScript type |
|-------------|-------------|
| integer types | `Number` (either through a call to `JsIntToNumber` or to `JsDoubleToNumber`, depending on type size and value) |
| floating-point types | `Number` (through a call to `JsDoubleToNumber`) |
| bool | `Boolean` |
| std::wstring | `String` |
| nullptr_t | Null (equivalent to calling `value::null`) |
| enum | `Number`, based on underlying integer type |

#### Creating Arrays

There are a number of static methods that can be used to construct JavaScript array objects:

```C++
// construct JavaScript array object and fill it with passed arguments
static value value::array(std::initializer_list<value> arguments);

// construct JavaScript array object and fill it with passed arguments
template<class... Args>
static value value::array(Args &&...args);

// construct JavaScript array object and fill it with passed arguments
// requires boost.range
template<class Range>
static value value::array_from_range(const Range &range);

// construct uninitialized JavaScript array object of a given size
static value value::uninitialized_array(unsigned int size = 0);
```

Sample usage:

```C++
auto array1 = jsc::value::array(true, false, 10, L"test"s);
std::vector<int> v {10, 20, 30};
auto array2 = jsc::value::array_from_range(v);
auto array3 = jsc::value::uninitialized_array(10);
```

`ArrayBuffer` object may be created using one of the following static methods:

```C++
// construct JavaScript ArrayBuffer object referencing external memory (check object lifetime!)
static value value::array_buffer(void *pdata, size_t size);
// construct JavaScript ArrayBuffer object referencing copy of external memory
static value value::array_buffer_copy(const void *pdata, size_t size);
```

The difference between them is that the second method copies the passed buffer and does not require that the buffer outlives the created array object.

Typed arrays are created with a call to a following static method:

```C++
static value value::typed_array(JsTypedArrayType arrayType, const value &baseArray, unsigned int byteOffset = 0, unsigned int elementLength = 0);
```

#### Converting `value` to C++ Types

Before we continue with functions and objects, let us describe how the values of class `value` may be converted back to C++ types.

There are several conversion options:

1. A number of as_xxx methods:
```C++
bool value::as_bool() const;
int value::as_int() const;
double value::as_double() const;
std::wstring value::as_string() const;
```

2. A templated `as` method:
```C++
auto int_value = v1.as<int>();
auto dbl_value = v2.as<double>();
auto bool_value = v3.as<bool>();
auto str_value = v4.as<std::wstring>();
auto uint_value = v5.as<unsigned>();
```

3. "Direct" conversion using static_cast:
```C++
auto str_value = static_cast<std::wstring>(v4);
```

Conversion to `enum` types are also supported.

Note that all conversion methods require the ChakraCore value be of correct or compatible type. For example, if you try to convert a string or array value into an integer, an exception is thrown.

You may use one of the following methods before converting to C++ type if you want to change the underlying JavaScript type:

```C++
value value::to_number() const;   // convert to number
value value::to_object() const;   // convert to object
value value::to_string() const;   // convert to string
```

Use the following method to determine the type of the ChakraCore value:

```C++
JsValueType value::value_type() const
```

In addition, the following methods may be used to test the properties of a type:

```C++
// Test if the value is empty (uninitialized)
bool value::is_empty() const noexcept;
bool value::empty() const noexcept;

// Test if value is null
bool value::is_null() const;

// Test if value is undefined
bool value::is_undefined() const;

// Test is value is a number
bool value::is_number() const;

// Test if value is a boolean
bool value::is_boolean() const;

// Test if value is string
bool value::is_string() const;

// Test if value is object
bool value::is_object() const;

// Test if value is array
bool value::is_array() const;

// Test if value is function
bool value::is_function() const;

// Test if value is typed array
bool value::is_typed_array() const;

// Test if value is an instance of ArrayBuffer class
bool value::is_array_buffer() const;

// Test if value is an instance of DataView class
bool value::is_data_view() const;
```

#### Creating Functions

One of the most powerful features of the ChakraCoreCppBridge library is an ability to easily bind a C++ function to a JavaScript function object. The JavaScript function object is created with a call to a following static method:

```C++
template<size_t ArgCount, class Callable>
value value::function(Callable callback);
```

Note that the caller **must** pass the number of callback function arguments as a first template parameter. The C++ language syntax does not allow the library to discover this information and it must be specified manually.

The `callback` parameter can be any C++ callable object, like a plain function pointer, function object, lambda, a result of a call to `std::bind` and so on. It is allowed to return value of any type implicitly convertible to `value` (see above) or be declared as `void`.

Callable object may have any number of arguments of any compatible type. `value` type is also allowed and is useful to consume JavaScript objects or optional arguments. It is recommended to take string arguments by reference (`const std::wstring &`) to avoid extra copies.

If the callback returns void, `undefined` will be returned to JavaScript code.

ChakraCore explicitly forbids exceptions flowing through the API boundary. ChakraCoreCppBridge library tries to catch any unhandled exception and convert it into JavaScript exception. That means that the calling JavaScript code may use the language construct to catch exceptions thrown by C++ code.

The library defines a `callback_exception` class. If the callback function should throw, it is recommended to throw an instance of this class or at least an instance of a class derived from `std::exception`.

`callback_exception::callback_exception` takes a single `std::wstring` value with a description of an error which is then propagated to caller in JavaScript `Error` object. If an instance of `std::exception` (or derived class) is thrown, library calls the `what` method and uses the returned string to construct JavaScript `Error` object. Otherwise, a generic "Unhandled Exception" message is used.

The library allows the JavaScript to call the passed C++ callback with unmatched number of arguments. If more arguments are passed, extra arguments are lost. If less arguments are passed, the remaining are assumed to be empty `value`  values. This means that if the callback is about to have optional parameters, they all have to be of type `value`, otherwise the exception will be thrown during implicit conversion and will propagate to a caller. Although, this might be a required behavior.

Sample code:

```C++
auto f = jsc::value::function<2>([](const std::wstring &text, int order)
{
    return text + std::to_wstring(order);
});
```

#### Calling JavaScript Functions

In addition to the ability to create JavaScript function objects with C++ callbacks, the library simplifies the calling of JavaScript functions. An overloaded `operator ()` is used for this:

```C++
template<class...Args>
value value::operator()(Args &&...args) const;
value value::operator()(const value *begin, const value *end) const;
```

The first overload allows you to directly pass function arguments. As usual, you may pass arguments of any type allowed in `value` constructor.

**Important!** ChakraCore exposes the first parameter as `this` to the callee. If you are calling a global function, remember passing the `nullptr` as a first argument. If you are calling a method of a "class", you must manually pass a reference to an object as a first parameter.

The second overload is provided for convenience.

#### Accessing Object Properties

Overloaded `operator []` is used to access the JavaScript object properties. It returns a proxy object (not the `value` directly), allowing you not only to query property values, but also to assign them:

```C++
auto prop = obj[L"propName"];
int prop_value = static_cast<int>(prop);
obj[L"propName"] = prop_value;
```

The proxy object is designed to seamlessly inter-operate with `value` and provides the same conversion methods and operators like `operator []` (nested properties) and `operator ()` (function calls).

```C++
auto value::operator [](JsPropertyIdRef propid) const;
auto value::operator [](const wchar_t *propname) const;
auto value::operator [](value index) const;
```

As you see, you can pass either the value name or property identifier.

Third overload is for working with indexed properties. Working with indexed properties is also possible with following methods:

```C++
void value::set_indexed(value ordinal, const value &value);
value value::get_indexed(value ordinal) const;
```

A wrapper method around `JsDefineProperty` is also provided in two overloads:

```C++
bool value::define_property(JsPropertyIdRef id, const value &descriptor) const;
bool value::define_property(const wchar_t *propname, const value &descriptor) const;
```

Use the `value::prototype` method to get object's prototype.

#### Creating Objects

The second key feature of ChakraCoreCppBridge library is an ability to easily define JavaScript objects backed by C++. Let us first see an example:

```C++
int c = 42;
auto obj = jsc::value::object()
	.field(L"a", 10)	// constant property value
	.property(L"b", [] { return L"Read-only property"; })
	.property(L"c", [&] { return c; }, [&](int new_c) { c = new_c; })
	.method<1>(L"print", [](const std::wstring &message)
{
	std::wcout << message << L"\r\n";
});
```

This example creates an object (by calling static `value::object()` function) and then defines its "structure". First it defines a property `a` with a constant value `10`. Then it adds two properties for which a getter lambda and setter lambda are specified. And finally, it declares a method `print` taking a single string value and returning nothing.

Here's the definition of used methods:

```C++
// Constant property
value value::field(const wchar_t *name, value value) const;

// Method. Remember to pass a number of method arguments
template<size_t ArgCount, class Callable>
value value::method(const wchar_t *name, Callable &&handler) const;

// Read-only property with getter
template<class Getter>
value value::property(const wchar_t *name, Getter &&getter) const;

// Read-write property with getter and setter
template<class Getter, class Setter>
value value::property(const wchar_t *name, Getter &&getter, Setter &&setter) const;
```

There is also an overload of `value::object` method taking a pointer to `IUnknown` interface. It makes sure the COM object is not deleted until the ChakraCore garbage collector deletes the JavaScript object.

##### Creating Dual Interfaces for C++ and JavaScript

Combined with a few macros in `chakra_macros.h` (requires boost.preprocessor library), it allows to easily expose C++ interfaces to JavaScript:

```C++
#include <chakra_bridge/chakra_macros.h>

struct ISomeObject
{
    // Define read-only property a (type int)
    JSC_DECLARE_PROP_GET(int, a)

    // Define read-write property b (type bool)
    JSC_DECLARE_PROP(bool, b)
    
    virtual void print(const std::wstring &)=0;
};

class SomeObject : public ISomeObject
{
    bool b{false};
public:
    // This method creates a JavaScript object 
    // representing this C++ object
    jsc::value to_javascript_object() const
    {
        return jsc::value::object()
            JSC_PROP_GET(a)
            JSC_PROP(b)
            JSC_METHOD(1, print)
            ;
    }

    // Property a getter
    virtual int get_a() override
    {
        return 42;
    }

    // Property b getter
    virtual bool get_b() override
    {
        return b;
    }

    // Property b setter
    virtual void set_b(bool v) override
    {
        b = v;
    }

    // Method print
    virtual void print(const std::wstring &) override
    {
        // ...
    }
};
```

This code shows how easy it is to create an interface consumable both by C++ and JavaScript. `ISomeObject` may also derive from `IUnknown`, in which case a call to `jsc::value::object()` is replaced with `jsc::value::object(this)`.

### `referenced_value` class

If you need to store `value` objects outside of the current scope, use the `referenced_value` class:

**Bad code:**

```C++
class Some
{
    jsc::runtime runtime;
    jsc::context context;
    jsc::value obj;

public:
    Some()
    {
        // initialize runtime, context and execute script
        // ...
        obj = jsc::value::object(); // BAD!!!
    }
};
```

**Correct code:**

```C++
class Some
{
    jsc::runtime runtime;
    jsc::context context;
    jsc::referenced_value obj;

public:
    Some()
    {
        // initialize runtime, context and execute script
        // ...
        obj = jsc::value::object(); // Correct
    }
};
```

### Exception Handling

For convenience, library tries to work with instances of `value` class directly, that is, take them as arguments and return them as resulting values. If ChakraCore reports an error during its execution, the error information is packaged into the instance of `exception` class and thrown. Use the `exception::code` method to get the `JsErrorCode` of a failed operation or call the `exception::to_js_exception` method to create a JavaScript `Error` object with the description.

The following helper functions are defined:

```C++
// True if failing error code
bool failed(JsErrorCode error) noexcept;

// True if successful error code
bool succeeded(JsErrorCode error) noexcept;

// Throw an exception if failed(error) is true
void check(JsErrorCode error);
```

These functions are also brought into global namespace, unless the `CBRIDGE_NO_GLOBAL_NAMESPACE` preprocessor constant is defined.

#### Getting Exception Information

```C++
std::tuple<remapped_error, std::wstring> print_exception(JsErrorCode code);
```

Use this function to convert the exception information to a text description with optional line and position of an error. See the source code for more options.

### Running Scripts

The library provides wrappers for a number of ChakraCore functions that are used to execute scripts. They are exactly the same as their ChakraCore counterparts except that they return `value` objects directly and throw if an error occurs:

```C++
value RunScript(const wchar_t *script, JsSourceContext sourceContext, const wchar_t *sourceUrl);
value ParseScript(const wchar_t *script, JsSourceContext sourceContext, const wchar_t *sourceUrl);
value ParseScriptWithAttributes(const wchar_t *script, JsSourceContext sourceContext, const wchar_t *sourceUrl, JsParseScriptAttributes parseAttributes);
value ExperimentalApiRunModule(const wchar_t *script, JsSourceContext sourceContext, const wchar_t *sourceUrl);
```
