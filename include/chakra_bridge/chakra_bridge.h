#pragma once

// STL
#include <tuple>
#include <functional>
#include <algorithm>
#include <string>
#include <type_traits>
#include <utility>
#include <stdexcept>
#include <array>
#include <locale>
#include <codecvt>
#include <cassert>
#include <memory>

// ChakraCore
#include <ChakraCore/inc/chakracommon.h>

#pragma push_macro("max")
#undef max

namespace jsc
{
	namespace details
	{
		using position_conversion_functor_t = std::function<std::pair<int, int>(int line, int pos)>;
		inline position_conversion_functor_t identity()
		{
			return [](int line, int pos) { return std::make_pair(line, pos); };
		}

		enum class remapped_error
		{
			InvalidArgument,
			NullArgument,
			NotAnObject,
			OutOfMemory,
			ScriptError,
			SyntaxError,
			FatalError,
			Exception,
			Unexpected,
		};

		inline remapped_error map_error(JsErrorCode code) noexcept
		{
			switch (code)
			{
			case JsErrorCode::JsErrorInvalidArgument:
				return remapped_error::InvalidArgument;
			case JsErrorCode::JsErrorNullArgument:
				return remapped_error::NullArgument;
			case JsErrorCode::JsErrorArgumentNotObject:
				return remapped_error::NotAnObject;
			case JsErrorCode::JsErrorOutOfMemory:
				return remapped_error::OutOfMemory;
			case JsErrorCode::JsErrorScriptException:
				return remapped_error::ScriptError;
			case JsErrorCode::JsErrorScriptCompile:
				return remapped_error::SyntaxError;
			case JsErrorCode::JsErrorFatal:
				return remapped_error::FatalError;
			case JsErrorCode::JsErrorInExceptionState:
				return remapped_error::Exception;
			default:
				return remapped_error::Unexpected;
			}
		}

		inline bool failed(JsErrorCode error) noexcept
		{
			return error != JsNoError;
		}

		inline bool succeeded(JsErrorCode error) noexcept
		{
			return error == JsNoError;
		}

		class value;

		class exception
		{
			JsErrorCode error;

		public:
			exception(JsErrorCode error) noexcept :
				error{ error }
			{}

			JsErrorCode code() const noexcept
			{
				return error;
			}

			value to_js_exception() const;
			value to_js_exception(const position_conversion_functor_t &posmap) const;
		};

		class callback_exception : public std::runtime_error
		{
			std::wstring message_;

		public:
			template<class String>
			callback_exception(String &&message) :
				message_{ std::forward<String>(message) },
				std::runtime_error{ "" }
			{}

			const auto &message() const
			{
				return message_;
			}
		};

		inline void check(JsErrorCode error)
		{
			if (failed(error))
				throw exception(error);
		}

		class runtime
		{
			JsRuntimeHandle handle{ JS_INVALID_RUNTIME_HANDLE };

		public:
			runtime(const runtime &) = delete;
			runtime &operator =(const runtime &) = delete;

			runtime(runtime &&o) noexcept :
			handle(o.handle)
			{
				o.handle = JS_INVALID_RUNTIME_HANDLE;
			}

			runtime &operator =(runtime &&o) noexcept
			{
				using std::swap;
				swap(handle, o.handle);
				return *this;
			}

			runtime() = default;
			~runtime()
			{
				if (handle != JS_INVALID_RUNTIME_HANDLE)
				{
					JsSetCurrentContext(JS_INVALID_REFERENCE);
					JsDisposeRuntime(handle);
				}
			}

			JsErrorCode create(JsRuntimeAttributes attributes, JsThreadServiceCallback threadService = nullptr) noexcept
			{
				return JsCreateRuntime(attributes, threadService, &handle);
			}

			operator JsRuntimeHandle() const noexcept
			{
				return handle;
			}
		};

		class context
		{
			JsContextRef handle{ nullptr };

		public:
			JsErrorCode create(JsRuntimeHandle runtime) noexcept
			{
				return JsCreateContext(runtime, &handle);
			}

			operator JsContextRef() const noexcept
			{
				return handle;
			}
		};

		class scoped_context
		{
		public:
			scoped_context(JsContextRef context)
			{
				check(JsSetCurrentContext(context));
			}

			~scoped_context()
			{
				auto success = succeeded(JsSetCurrentContext(JS_INVALID_REFERENCE));
				success;
				assert(success && "Error exiting context");
			}
		};

		// forward declare property access proxies
		class prop_ref_propid;
		class prop_ref_indexed;

		template<class Base>
		class prop_ref;

		// type dispatch helpers
		template<class T>
		struct is_string_v
		{
			static const bool value = std::is_same<std::wstring, T>::value;
		};

		template<class T>
		struct is_bool_v
		{
			static const bool value = std::is_same<bool, T>::value;
		};

		template<class T>
		using is_enum_v = std::is_enum<T>;

		template<class T>
		struct is_small_int_v
		{
			static const bool value = !is_enum_v<T>::value && !is_bool_v<T>::value && std::is_integral<T>::value && sizeof(T) <= sizeof(int) &&
				!std::is_same<char, T>::value && !std::is_same<wchar_t, T>::value;
		};

		template<class T>
		struct is_big_number_v
		{
			static const bool value = !is_enum_v<T>::value &&
				(
					std::is_floating_point<T>::value ||
					(std::is_integral<T>::value && !is_bool_v<T>::value && !is_small_int_v<T>::value && sizeof(T)>sizeof(int))
					);
		};

		template<class T>
		struct is_supported_v
		{
			static const bool value =/*is_string<T>::value && */is_enum_v<T>::value || is_bool_v<T>::value || is_small_int_v<T>::value || is_big_number_v<T>::value;
		};

		class referenced_value;

		class value
		{
			JsValueRef val{ JS_INVALID_REFERENCE };

			// helper functions to call C++ functions with passed arguments
			template<typename F, class T, size_t N, std::size_t... I>
			static auto apply_helper(const F &f, const std::array<T, N> &params, std::index_sequence<I...>)
			{
				(params);
				return std::invoke(f, params[I]...);
			}

			template<typename F, class T, size_t N>
			static auto apply(const F &f, const std::array<T, N> &params)
			{
				return apply_helper(f, params, std::make_index_sequence<N>{});
			}

			template<size_t N, class Callable>
			static value execute_functor_helper(const Callable &f, const std::array<value, N> &values, std::true_type)
			{
				// void
				apply(f, values);
				return undefined();
			}

			template<size_t N, class Callable>
			static value execute_functor_helper(const Callable &f, const std::array<value, N> &values, std::false_type)
			{
				return value{ apply(f, values) };
			}

			template<size_t N, class Callable>
			static value execute_functor(const Callable &f, const std::array<value, N> &values)
			{
				return execute_functor_helper(f, values, std::is_same<void, decltype(apply(f, values))>{});
			}

			// helpers to construct value from different types
			static JsValueRef from(const std::wstring &text)
			{
				JsValueRef result;
				check(JsPointerToString(text.c_str(), text.size(), &result));
				return result;
			}

			static JsValueRef from(const wchar_t *text)
			{
				JsValueRef result;
				check(JsPointerToString(text, wcslen(text), &result));
				return result;
			}

			static JsValueRef from(double v)
			{
				JsValueRef result;
				check(JsDoubleToNumber(v, &result));
				return result;
			}

			static JsValueRef from(int v)
			{
				JsValueRef result;
				check(JsIntToNumber(v, &result));
				return result;
			}

			static JsValueRef from(bool v)
			{
				JsValueRef result;
				check(JsBoolToBoolean(v, &result));
				return result;
			}

			static JsValueRef from(nullptr_t)
			{
				JsValueRef result;
				check(JsGetNullValue(&result));
				return result;
			}

			// enum
			template<class T>
			static std::enable_if_t<is_enum_v<T>::value, JsValueRef> from(T val)
			{
				return from(static_cast<std::underlying_type_t<T>>(val));
			}

			template<class T>
			static std::enable_if_t<is_big_number_v<T>::value, JsValueRef> from(T val)
			{
				return from(static_cast<double>(val));
			}

			template<class T>
			static std::enable_if_t<is_small_int_v<T>::value, JsValueRef> from(T val)
			{
				return from(val, std::integral_constant<bool, sizeof(T) < sizeof(int) || std::is_signed<T>::value > {});
			}

			template<class T>
			static JsValueRef from(T val, std::true_type)
			{
				return from(static_cast<int>(val));
			}

			template<class T>
			static JsValueRef from(T val, std::false_type)
			{
				if (val > static_cast<T>(std::numeric_limits<int>::max()))
					return from(static_cast<double>(val));
				else
					return from(static_cast<int>(val));
			}

			// special integer constructors
			// construct through integer
			template<class T>
			value(T val, std::true_type) noexcept :
				val{ from(static_cast<int>(val)) }
			{}

			template<class T>
			value(T val, std::false_type) noexcept :
				val{ from(static_cast<double>(val)) }
			{}

			// value retrieval
			// string
			template<class T>
			T as(std::true_type string, std::false_type, std::false_type, std::false_type, std::false_type) const
			{
				string;
				return as_string();
			}

			// bool
			template<class T>
			T as(std::false_type, std::true_type boolean, std::false_type, std::false_type, std::false_type) const
			{
				boolean;
				return as_bool();
			}

			// enum
			template<class T>
			T as(std::false_type, std::false_type, std::true_type enumeration, std::false_type, std::false_type) const
			{
				enumeration;
				return static_cast<T>(as<std::underlying_type_t<T>>());
			}

			// big number
			template<class T>
			T as(std::false_type, std::false_type, std::false_type, std::true_type bignum, std::false_type) const
			{
				bignum;
				return static_cast<T>(as_double());
			}

			// small number
			template<class T>
			T as(std::false_type, std::false_type, std::false_type, std::false_type, std::true_type smallnum) const
			{
				smallnum;
				return as_small_int<T>(std::integral_constant<bool, sizeof(T) < sizeof(int) || std::is_signed<T>::value > {});
			}

			template<class T>
			T as_small_int(std::true_type) const
			{
				return static_cast<T>(as_int());
			}

			template<class T>
			T as_small_int(std::false_type) const
			{
				using namespace std::string_literals;
				auto val = as_int();
				if (val < 0)
				{
					JsValueRef error;
					check(JsCreateRangeError(value{ L"Value is out of range" }, &error));
					JsSetException(error);
					throw exception(JsErrorScriptException);
				}
				else
					return static_cast<T>(val);
			}
		public:
			// static members that construct different types of ChakraCore values

			// construct JavaScript array object and fill it with passed arguments
			static value array(std::initializer_list<value> arguments)
			{
				JsValueRef result_;
				check(JsCreateArray(static_cast<unsigned int>(arguments.size()), &result_));
				auto result = value{ result_ };
				for (size_t i = 0; i < arguments.size(); ++i)
					result.set_indexed((int)i, arguments.begin()[i]);

				return result;
			}

			// construct JavaScript array object and fill it with passed arguments
			template<class... Args>
			static value array(Args &&...args)
			{
				return array({ std::forward<Args>(args)... });
			}

			// construct JavaScript array object and fill it with passed arguments
			// requires boost.range
			template<class Range>
			static value array_from_range(const Range &range)
			{
				const auto size = boost::size(range);
				JsValueRef result_;
				check(JsCreateArray((unsigned)size, &result_));
				auto result = value{ result_ };
				int i = 0;
				for (const auto &e : range)
					result.set_indexed(i++, e);
				return result;
			}

			// construct JavaScript array object of a given size
			static value array(unsigned int size = 0)
			{
				JsValueRef result;
				check(JsCreateArray(size, &result));
				return value{ result };
			}

			// construct JavaScript ArrayBuffer object referencing external memory (check object lifetime!)
			static value array_buffer(void *pdata, size_t size)
			{
				JsValueRef result;
				check(JsCreateExternalArrayBuffer(pdata, (unsigned int)size, nullptr, nullptr, &result));
				return value{ result };
			}

			// construct JavaScript ArrayBuffer object referencing copy of external memory
			static value array_buffer_copy(const void *pdata, size_t size)
			{
				auto copy = std::make_unique<BYTE[]>(size);
				memcpy(copy.get(), pdata, size);
				JsValueRef result;
				check(JsCreateExternalArrayBuffer(copy.get(), (unsigned int)size, [](void *data)
				{
					std::unique_ptr<BYTE[]> d(static_cast<BYTE *>(data));
				}, copy.get(), &result));
				copy.release();	// will be deleted later in callback
				return value{ result };
			}

			// construct JavaScript TypedArray object
			static value typed_array(JsTypedArrayType arrayType, const value &baseArray, unsigned int byteOffset = 0, unsigned int elementLength = 0)
			{
				JsValueRef result;
				check(JsCreateTypedArray(arrayType, baseArray, byteOffset, elementLength, &result));
				return value{ result };
			}

			// return null JavaScript value
			static value null()
			{
				return value{ nullptr };
			}

			// return undefined JavaScript value
			static value undefined()
			{
				JsValueRef result;
				check(JsGetUndefinedValue(&result));
				return value{ result };
			}

			// return true boolean value
			static value true_()
			{
				JsValueRef result;
				check(JsGetTrueValue(&result));
				return value{ result };
			}

			// return false boolean value
			static value false_()
			{
				JsValueRef result;
				check(JsGetFalseValue(&result));
				return value{ result };
			}

			// construct JavaScript function object. When invoked, a passed function is called with parameters converted
			template<size_t ArgCount, class Callable>
			static value function(Callable function)
			{
				using namespace std::string_literals;
				auto pfcopy = std::make_unique<Callable>(std::move(function));
				JsValueRef result;
				check(JsCreateFunction([](JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *pfcopy)->JsValueRef
				{
					callee;
					isConstructCall;
					auto runtime_args = argumentCount - 1;
					auto begin = arguments + 1;

					auto *pf = static_cast<Callable *>(pfcopy);
					try
					{
						if (runtime_args >= ArgCount)
							return execute_functor<ArgCount>(*pf, *reinterpret_cast<std::array<value, ArgCount> *>(begin));
						else
						{
							std::array<value, ArgCount> params;
							auto cbegin = reinterpret_cast<value *>(begin);
							std::copy(cbegin, cbegin + runtime_args, params.begin());
							return execute_functor<ArgCount>(*pf, params);
						}
					}
					catch (const exception &e)
					{
						return e.to_js_exception();
					}
					catch (const callback_exception &e)
					{
						JsValueRef result;
						check(JsCreateError(value{ e.message() }, &result));
						JsSetException(result);
						return result;
					}
					catch (const std::exception &e)
					{
						JsValueRef result;
						check(JsCreateError(value{ std::wstring_convert<std::codecvt_utf8<wchar_t>>{}.from_bytes(e.what()) }, &result));
						JsSetException(result);
						return result;
					}
					catch (...)
					{
						JsValueRef result;
						check(JsCreateError(value{ L"Unknown error"s }, &result));
						JsSetException(result);
						return result;
					}
				}, pfcopy.get(), &result));

				check(JsSetObjectBeforeCollectCallback(result, pfcopy.get(), [](JsRef ref, void *callbackState)
				{
					ref;
					delete static_cast<Callable *>(callbackState);
				}));
				pfcopy.release();	// will be deleted in callback above
				return value{ result };
			}

			// return and clear the current runtime exception
			static value current_exception()
			{
				JsValueRef result;
				check(JsGetAndClearException(&result));
				return value{ result };
			}

			// return a global object
			static value global()
			{
				JsValueRef global;
				check(JsGetGlobalObject(&global));
				return value{ global };
			}

			// construct new JavaScript object
			static value object()
			{
				JsValueRef result;
				check(JsCreateObject(&result));
				return value{ result };
			}

			// construct new JavaScript object based on COM object
			static value object(IUnknown *pObj)
			{
				JsValueRef result;
				check(JsCreateExternalObject(pObj, [](void *p)
				{
					static_cast<IUnknown *>(p)->Release();
				}, &result));
				pObj->AddRef();
				return value{ result };
			}

			static value ref(JsValueRef v) noexcept
			{
				return value{ v };
			}

			// default constructor
			value() = default;
			// destructor
			// explicit value ref constructor
			explicit value(JsValueRef v) noexcept :
				val{ v }
			{
			}

			// Conversion constructors
			// bool
			value(bool v) :
				val{ from(v) }
			{
			}

			// null
			value(nullptr_t) :
				val{ from(nullptr) }
			{
			}

			// string
			value(const std::wstring &text) :
				val{ from(text) }
			{
			}

			value(const wchar_t *text) :
				val{ from(text) }
			{
			}

			// double
			value(double v) :
				val{ from(v) }
			{
			}

			// integer
			value(int v) :
				val{ from(v) }
			{
			}

			value(const referenced_value &);

			template<class T>
			value(T v) :
				val{ from(v) }
			{}

			template<class Base>
			value(const prop_ref<Base> &prop);

			// value type retrieval
			JsValueType value_type() const
			{
				JsValueType result;
				check(JsGetValueType(val, &result));
				return result;
			}

			// conversion functions
			value to_object() const
			{
				JsValueRef result;
				check(JsConvertValueToObject(val, &result));
				return value{ result };
			}

			value to_number() const
			{
				JsValueRef result;
				check(JsConvertValueToNumber(val, &result));
				return value{ result };
			}

			std::wstring to_string() const
			{
				JsValueRef result;
				check(JsConvertValueToString(val, &result));
				return static_cast<std::wstring>(value{ result });
			}

			// prototype
			value prototype() const
			{
				JsValueRef result;
				check(JsGetPrototype(val, &result));
				return value{ result };
			}

			// index operators
			prop_ref<prop_ref_indexed> operator [](value index) const;
			prop_ref<prop_ref_propid> operator[](JsPropertyIdRef propid) const;
			prop_ref<prop_ref_propid> operator [](LPCTSTR propname) const;

			void set_indexed(value ordinal, const value &value) const
			{
				check(JsSetIndexedProperty(this->val, ordinal, value));
			}

			value get_indexed(value ordinal) const
			{
				JsValueRef result;
				check(JsGetIndexedProperty(this->val, ordinal, &result));
				return value{ result };
			}

			// properties
			void set(LPCTSTR propname, const value &value) const
			{
				JsPropertyIdRef propid;
				check(JsGetPropertyIdFromName(propname, &propid));
				set(propid, value);
			}

			void set(JsPropertyIdRef propid, const value &value) const
			{
				check(JsSetProperty(this->val, propid, value, true));
			}

			bool define_property(JsPropertyIdRef id, const value &descriptor) const
			{
				bool result;
				check(JsDefineProperty(val, id, descriptor, &result));
				return result;
			}

			bool define_property(LPCTSTR propname, const value &descriptor) const
			{
				JsPropertyIdRef propid;
				check(JsGetPropertyIdFromName(propname, &propid));
				return define_property(propid, descriptor);
			}

			template<size_t ArgCount, class Callable>
			value method(const wchar_t *name, Callable &&handler) const
			{
				(*this)[name] = function<ArgCount>(std::forward<Callable>(handler));
				return *this;	// copies are cheap
			}

			value field(const wchar_t *name, value value_) const;

			template<class Getter>
			value property(const wchar_t *name, Getter &&getter) const
			{
				define_property(name, object()
					.field(L"configurable", false_())
					.field(L"get", function<0>(std::forward<Getter>(getter)))
					.field(L"set", function<1>([name = std::wstring{ name }](value)->value
				{
					using namespace std::string_literals;
					JsValueRef exc;
					check(JsCreateError(value{ name + L": property is read-only"s }, &exc));
					check(JsSetException(exc));
					return value{ exc };
				})))
					;
				return *this;
			}

			template<class Getter, class Setter>
			value property(const wchar_t *name, Getter &&getter, Setter &&setter) const
			{
				define_property(name, object()
					.field(L"configurable", false_())
					.field(L"get", function<0>(std::forward<Getter>(getter)))
					.field(L"set", function<1>(std::forward<Setter>(setter)))
				);
				return *this;
			}

			// function call
			value operator()(std::initializer_list<value> arguments) const
			{
				JsValueRef result;
				check(JsCallFunction(val, reinterpret_cast<JsValueRef *>(const_cast<value *>((arguments.begin()))), (unsigned short)arguments.size(), &result));
				return value{ result };
			}

			value operator()(const value *begin, const value *end) const
			{
				JsValueRef result;
				check(JsCallFunction(val, reinterpret_cast<JsValueRef *>(const_cast<value *>(begin)), (unsigned short)std::distance(begin, end), &result));
				return value{ result };
			}

			template<class...Args>
			value operator()(Args &&...args) const
			{
				return operator()({ value{ std::forward<Args>(args) }... });
			}

			// value accessors
			operator JsValueRef() const noexcept
			{
				return val;
			}

			bool as_bool() const
			{
				bool result;
				check(JsBooleanToBool(val, &result));
				return result;
			}

			int as_int() const
			{
				int result;
				check(JsNumberToInt(val, &result));
				return result;
			}

			double as_double() const
			{
				double result;
				check(JsNumberToDouble(val, &result));
				return result;
			}

			std::wstring as_string()const
			{
				const wchar_t *ptr;
				size_t length;
				check(JsStringToPointer(val, &ptr, &length));
				return{ ptr, length };
			}

			template<class T>
			T as() const
			{
				return as<T>(
					std::integral_constant<bool, is_string_v<T>::value>{},
					std::integral_constant<bool, is_bool_v<T>::value>{},
					std::integral_constant<bool, is_enum_v<T>::value>{},
					std::integral_constant<bool, is_big_number_v<T>::value>{},
					std::integral_constant<bool, is_small_int_v<T>::value>{}
				);
			}

			operator std::wstring() const
			{
				return as_string();
			}

			template<class T, class U=std::enable_if_t<is_supported_v<T>::value>>
			operator T() const
			{
				return as<T>();
			}

			operator wchar_t()const = delete;
			operator char()const = delete;

			void *data() const
			{
				void *res;
				check(JsGetExternalData(val, &res));
				return res;
			}

			//
			bool is_empty() const noexcept
			{
				return val == JS_INVALID_REFERENCE;
			}

			bool empty() const noexcept
			{
				return is_empty();
			}

			bool is_null() const
			{
				return value_type() == JsNull;
			}

			bool is_undefined() const
			{
				return value_type() == JsUndefined;
			}

			bool is_string() const
			{
				return value_type() == JsString;
			}

			bool is_object() const
			{
				return value_type() == JsObject;
			}

			bool is_array() const
			{
				return value_type() == JsArray;
			}

			bool is_function() const
			{
				return value_type() == JsFunction;
			}

			bool is_typed_array() const
			{
				return value_type() == JsTypedArray;
			}

			bool is_array_buffer() const
			{
				return value_type() == JsArrayBuffer;
			}

			bool is_data_view() const
			{
				return value_type() == JsDataView;
			}

			bool is_number() const
			{
				return value_type() == JsNumber;
			}

			bool is_boolean() const
			{
				return value_type() == JsBoolean;
			}
		};

		class referenced_value : public value
		{
			void add_ref() noexcept
			{
				auto v = static_cast<JsValueRef>(*this);
				if (v != JS_INVALID_REFERENCE)
					JsAddRef(v, nullptr);
			}

			void release() noexcept
			{
				auto v = static_cast<JsValueRef>(*this);
				if (v != JS_INVALID_REFERENCE)
					JsRelease(v, nullptr);
			}

		public:
			using value::value;

			referenced_value() = default;
			referenced_value(const value &o) noexcept :
				value{ o }
			{
				add_ref();
			}

			template<class T>
			referenced_value(const T &o) noexcept :
				value{ o }
			{
				add_ref();
			}

			template<class Base>
			referenced_value(const prop_ref<Base> &prop) noexcept :
				value{ prop }
			{
				add_ref();
			}

			referenced_value(referenced_value &&o) :
				value{ static_cast<const value &>(o) }
			{
				static_cast<value &>(o) = value{ JS_INVALID_REFERENCE };
			}

			referenced_value &operator =(referenced_value &&o)
			{
				using std::swap;
				swap(static_cast<value &>(*this), static_cast<value &>(o));
				return *this;
			}

			~referenced_value()
			{
				release();
			}

			referenced_value(const referenced_value &o) noexcept :
				value{ static_cast<const value &>(o) }
			{
				add_ref();
			}

			referenced_value &operator =(const referenced_value &o)
			{
				release();
				static_cast<value &>(*this) = static_cast<const value &>(o);
				add_ref();
				return *this;
			}
		};

		class prop_ref_propid
		{
			JsPropertyIdRef propid;

		protected:
			prop_ref_propid(JsPropertyIdRef propid) noexcept :
				propid{ propid }
			{}

			auto get(value obj) const
			{
				JsValueRef result;
				check(JsGetProperty(obj, propid, &result));
				return value{ result };
			}

			void set(value obj, value value)
			{
				obj.set(propid, value);
			}
		};

		class prop_ref_indexed
		{
			value index;

		protected:
			prop_ref_indexed(value index) noexcept :
				index{ index }
			{}

			auto get(value obj) const
			{
				JsValueRef result;
				check(JsGetIndexedProperty(obj, index, &result));
				return value{ result };
			}

			void set(value obj, value value)
			{
				obj.set_indexed(index, value);
			}
		};

		template<class Base>
		class prop_ref : public Base
		{
			value obj;

			friend class value;

			template<class Index>
			prop_ref(value obj, const Index &index) noexcept :
			obj{ obj },
				Base{ index }
			{}

		public:
			value get() const
			{
				return Base::get(obj);
			}

			template<class T, class U=std::enable_if_t<is_supported_v<T>::value>>
			operator T()const
			{
				return as<T>();
			}

			operator std::wstring() const
			{
				return get().as_string();
			}

			template<class T>
			T as() const
			{
				return get().as<T>();
			}

			auto as_string() const
			{
				return as<std::wstring>();
			}

			template<class... Args>
			auto operator()(Args &&...args) const
			{
				return get()(std::forward<Args>(args)...);
			}

			prop_ref &operator =(value value)
			{
				Base::set(obj, value);
				return *this;
			}

			template<class Index>
			auto operator[](const Index &index) const
			{
				return get()[index];
			}

			auto to_number() const
			{
				return get().to_number();
			}

			auto to_object() const
			{
				return get().to_object();
			}

			auto to_string() const
			{
				return get().to_string();
			}

			auto value_type() const
			{
				return get().value_type();
			}
		};

		inline value::value(const referenced_value &o) :
			val{ o.val }
		{}

		inline prop_ref<prop_ref_propid> value::operator[](JsPropertyIdRef propid) const
		{
			return prop_ref<prop_ref_propid>{ *this, propid };
		}

		inline prop_ref<prop_ref_propid> value::operator [](LPCTSTR propname) const
		{
			JsPropertyIdRef propid;
			check(JsGetPropertyIdFromName(propname, &propid));
			return operator[](propid);
		}

		inline prop_ref<prop_ref_indexed> value::operator[](value index) const
		{
			return prop_ref<prop_ref_indexed>{ *this, index };
		}

		inline value value::field(const wchar_t *name, value value_) const
		{
			(*this)[name] = value_;
			return *this;
		}

		template<class Base>
		inline value::value(const prop_ref<Base> &prop) :
			value{ prop.get() }
		{
		}

		class exception_details : public value
		{
		public:
			exception_details() :
				value{ value::current_exception() }
			{
			}

			std::wstring message() const
			{
				try
				{
					return operator[](L"message").as_string();
				}
				catch (const exception &)
				{
					return{};
				}
			}

			std::wstring stack() const
			{
				try
				{
					return operator[](L"stack").as_string();
				}
				catch (const exception &)
				{
					return{};
				}
			}

			std::wstring description() const
			{
				try
				{
					return operator [](L"description").as_string();
				}
				catch (const exception &)
				{
					return{};
				}
			}
		};

		inline bool has_exception() noexcept
		{
			bool has;
			return JsNoError == JsHasException(&has) && has;
		}

		inline std::tuple<remapped_error, std::wstring> print_exception(JsErrorCode code, const position_conversion_functor_t &posmap)
		{
			using namespace std::string_literals;
			auto remappedcode = map_error(code);
			std::wstring message;

			try
			{
				if (code == JsErrorCode::JsErrorScriptCompile || code == JsErrorCode::JsErrorScriptException)
				{
					exception_details einfo;
					message = einfo.to_string();
					if (code == JsErrorCode::JsErrorScriptCompile)
					{
						auto line = einfo[L"line"].as<int>();
						auto column = einfo[L"column"].as<int>();

						std::tie(line, column) = posmap(line, column);

						message += L" ("s + std::to_wstring(line) + L":"s + std::to_wstring(column) + L")"s;
					}
					else
					{
						try
						{
							message = einfo[L"stack"].as_string();
						}
						catch (const exception &)
						{
						}
					}
				}
			}
			catch (const exception &)
			{
			}

			return{ remappedcode,message };
		}

		inline auto print_exception(JsErrorCode code)
		{
			return print_exception(code, identity());
		}

		inline auto print_exception(const exception &e, const position_conversion_functor_t &posmap)
		{
			return print_exception(e.code(), posmap);
		}

		inline auto print_exception(const exception &e)
		{
			return print_exception(e.code());
		}

		inline value exception::to_js_exception() const
		{
			return to_js_exception(identity());
		}

		inline value exception::to_js_exception(const position_conversion_functor_t &posmap) const
		{
			using namespace std::string_literals;
			auto einfo = print_exception(code(), posmap);
			try
			{
				JsValueRef exc;
				static const wchar_t *error_messages[] = {
					L"Invalid argument",
					L"Null argument",
					L"Argument not an object",
					L"Out of memory",
					L"Script error",
					L"Syntax error",
					L"Fatal error",
					L"Exception",
					L"Unexpected code"
				};
				check(JsCreateError(value{ error_messages[static_cast<int>(std::get<0>(einfo))] + L": "s + std::get<1>(einfo) }, &exc));
				check(JsSetException(exc));
				return value{ exc };
			}
			catch (const exception &)
			{
				return nullptr;
			}
		}

		// Run script helpers
		inline value RunScript(const wchar_t *script, JsSourceContext sourceContext, const wchar_t *sourceUrl)
		{
			JsValueRef result;
			check(JsRunScript(script, sourceContext, sourceUrl, &result));
			return value{ result };
		}

		inline value ParseScript(const wchar_t *script, JsSourceContext sourceContext, const wchar_t *sourceUrl)
		{
			JsValueRef result;
			check(JsParseScript(script, sourceContext, sourceUrl, &result));
			return value{ result };
		}

		inline value ParseScriptWithAttributes(const wchar_t *script, JsSourceContext sourceContext, const wchar_t *sourceUrl, JsParseScriptAttributes parseAttributes)
		{
			JsValueRef result;
			check(JsParseScriptWithAttributes(script, sourceContext, sourceUrl, parseAttributes, &result));
			return value{ result };
		}

		inline value ExperimentalApiRunModule(const wchar_t *script, JsSourceContext sourceContext, const wchar_t *sourceUrl)
		{
			JsValueRef result;
			check(JsExperimentalApiRunModule(script, sourceContext, sourceUrl, &result));
			return value{ result };
		}
	}

	// Bring several items into jsc namespace
	using details::value;
	using details::referenced_value;
	using details::exception;
	using details::runtime;
	using details::context;
	using details::exception_details;
	using details::print_exception;
	using details::position_conversion_functor_t;
	using details::remapped_error;
	using details::scoped_context;
	using details::callback_exception;
	using details::RunScript;
	using details::ParseScript;
	using details::ParseScriptWithAttributes;
	using details::ExperimentalApiRunModule;
}

// Bring several items into global namespace
using jsc::details::check;
using jsc::details::succeeded;
using jsc::details::failed;

#pragma pop_macro("max")
