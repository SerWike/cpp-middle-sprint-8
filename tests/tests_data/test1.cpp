#include <iostream>

class BaseNoDtor {
public:
    int x;
    ~BaseNoDtor() = default;
};

class DerivedFromNoDtor : public BaseNoDtor {};

class BaseNonVirtual {  // Невиртуальный деструктор
public:
    ~BaseNonVirtual() {}  //Убедимся что один раз virtual
};

class DerivedFromNonVirtual : public BaseNonVirtual {};

class DerivedFromNonVirtual1 : public DerivedFromNonVirtual {};

class BaseVirtual {  // Уже виртуальный деструктор, не меняется
public:
    virtual ~BaseVirtual() {}
};

class DerivedFromVirtual : public BaseVirtual {};

class Standalone {  // Нет наследников, не меняется
public:
    ~Standalone() {}
};

class DifDeclAndDefin
{
public:
    ~DifDeclAndDefin();
};
DifDeclAndDefin::~DifDeclAndDefin() {}

class Temp : public DifDeclAndDefin {};

template<typename T>
class TemplateBase {
public:
    ~TemplateBase();
};

class D1 : public TemplateBase<int> {
public:
    ~D1();
};

class D2 : public TemplateBase<double> {
public:
    ~D2();
};