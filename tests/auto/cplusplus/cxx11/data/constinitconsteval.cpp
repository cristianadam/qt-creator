constexpr const char* foo() { return "foo"; }
consteval int id(int n) { return n; }
constinit const char* c = foo();
constexpr int n = id(0);
