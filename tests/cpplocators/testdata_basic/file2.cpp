// Copyright header to keep the Qt Insanity Bot happy.

//
// Aliases, nested types and class members, which file1.cpp does not have.
//

typedef int MyTypedef;

using MyAlias = MyTypedef;

extern int declaredVariable;

struct Outer
{
    struct Inner
    {
        int innerField;
    };

    enum class ScopedEnum { First, Second };

    static int staticVariable;
    int field;

    static int staticFunction(int number);
    int operator+(int rhs) const;
    virtual ~Outer();
};

int Outer::staticVariable = 0;

int Outer::staticFunction(int number) { return number; }

Outer::~Outer() {}

union MyUnion
{
    int asInt;
    float asFloat;
};

template<typename T>
T templateFunction(T value) { return value; }

namespace MyOtherNamespace {

using AliasInNamespace = Outer::Inner;

typedef Outer::ScopedEnum TypedefInNamespace;

} // namespace MyOtherNamespace
