class S
{
public:
    S();
    S(const char *text);
    bool isEmpty() const;
    void clear();
};

void f1(S s);
void f2(S *s);

void foo() {
    S *str = new S;
    str->clear();
    {
        S str;
        str.clear();
    }
    f1(*str);
}
