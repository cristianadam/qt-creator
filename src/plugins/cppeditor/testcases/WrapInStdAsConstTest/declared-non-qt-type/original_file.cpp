template<typename T> class Container
{
public:
    T *begin();
    T *end();
};

void f()
{
    Container<int> c;
    for (int x : @c) {}
}
