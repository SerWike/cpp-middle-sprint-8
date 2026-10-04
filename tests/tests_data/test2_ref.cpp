#include <string>

class Base {
public:
    virtual void func() {}
    virtual void func(int a) = 0;
    virtual ~Base() {}
};

class Derived : public Base {
public:
    void func() override {}  // Переопределен без override
    void func(int a) override {}  // Переопределен без override
    ~Derived() {}  // Деструктор без override
};

class SubDerived : public Derived {
public:
    void func() override {}  // Переопределен без override
    void func(int a) override {}  // Переопределен без override
};

class BaseWithOverride {
public:
    virtual void func() {}
};

class DerivedWithOverride : public BaseWithOverride {
public:
    void func() override {}  // Уже с override, не меняется
};

class BaseWithHiding {
public:
    void func();
};

class DerivedWithHiding : public BaseWithHiding {
public:
    void func() {};
};

struct BaseWithoutConst {
    virtual void func();
};

struct DerivedWithConst : BaseWithoutConst {
    void func() const {};
};

struct PureVirtualBase {
    virtual void func() = 0;
};

struct DerivedFromPureVirtual : PureVirtualBase {
    void func() override {};
};