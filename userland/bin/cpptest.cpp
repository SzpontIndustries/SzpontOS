/*
 * SzpontOS C++ Runtime and Standard Library Validation Suite
 * (C) Copyright by Szpont Industries. All rights reserved.
 */

#include <iostream>
#include <vector>
#include <string>
#include <memory>
#include <algorithm>
#include <typeinfo>

static int g_constructor_invoked = 0;

class GlobalConstructedTest {
public:
    GlobalConstructedTest() {
        g_constructor_invoked = 42;
    }
    ~GlobalConstructedTest() {
        // Will be called during atexit
    }
};

static GlobalConstructedTest g_test_instance;

class Base {
public:
    virtual ~Base() = default;
    virtual const char *name() const { return "Base"; }
};

class Derived : public Base {
public:
    const char *name() const override { return "Derived"; }
};

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    std::cout << "========================================" << std::endl;
    std::cout << "   SzpontOS Native C++ Runtime Tests    " << std::endl;
    std::cout << "========================================" << std::endl;

    // Test 1: Global constructors (.init_array)
    std::cout << "[TEST 1] Global Constructor (.init_array): ";
    if (g_constructor_invoked == 42) {
        std::cout << "PASSED (value=" << g_constructor_invoked << ")" << std::endl;
    } else {
        std::cout << "FAILED (value=" << g_constructor_invoked << ")" << std::endl;
        return 1;
    }

    // Test 2: Standard I/O Streams and strings
    std::cout << "[TEST 2] std::string & std::cout: ";
    std::string greeting = "Hello, ";
    greeting += "C++ World on SzpontOS!";
    std::cout << greeting << " -> PASSED" << std::endl;

    // Test 3: STL Vector and Algorithms
    std::cout << "[TEST 3] std::vector & std::sort: ";
    std::vector<int> numbers = { 42, 7, 13, 99, 1, 23 };
    std::sort(numbers.begin(), numbers.end());
    bool sorted = true;
    for (size_t i = 1; i < numbers.size(); ++i) {
        if (numbers[i - 1] > numbers[i]) {
            sorted = false;
            break;
        }
    }
    if (sorted && numbers.front() == 1 && numbers.back() == 99) {
        std::cout << "PASSED (sorted " << numbers.size() << " elements)" << std::endl;
    } else {
        std::cout << "FAILED" << std::endl;
        return 2;
    }

    // Test 4: Smart Pointers
    std::cout << "[TEST 4] std::unique_ptr & std::shared_ptr: ";
    {
        std::unique_ptr<int> uptr = std::make_unique<int>(12345);
        std::shared_ptr<int> sptr = std::make_shared<int>(67890);
        if (*uptr == 12345 && *sptr == 67890) {
            std::cout << "PASSED" << std::endl;
        } else {
            std::cout << "FAILED" << std::endl;
            return 3;
        }
    }

    // Test 5: Virtual Dispatch & RTTI
    std::cout << "[TEST 5] Polymorphism & RTTI dynamic_cast: ";
    std::unique_ptr<Base> obj = std::make_unique<Derived>();
    Derived *d = dynamic_cast<Derived*>(obj.get());
    if (d != nullptr && std::string(obj->name()) == "Derived") {
        std::cout << "PASSED (type=" << typeid(*obj).name() << ")" << std::endl;
    } else {
        std::cout << "FAILED" << std::endl;
        return 4;
    }

    // Test 6: Exceptions
    std::cout << "[TEST 6] Exception Handling (try/catch): ";
    try {
        throw 100;
    } catch (int e) {
        if (e == 100) {
            std::cout << "PASSED (caught code=" << e << ")" << std::endl;
        } else {
            std::cout << "FAILED" << std::endl;
            return 5;
        }
    } catch (...) {
        std::cout << "FAILED (unexpected catch)" << std::endl;
        return 5;
    }

    std::cout << "========================================" << std::endl;
    std::cout << "ALL C++ RUNTIME TESTS COMPLETED SUCCESSFULLY!" << std::endl;
    std::cout << "========================================" << std::endl;

    return 0;
}
