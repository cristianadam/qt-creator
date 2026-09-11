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
    S other;
    S *str = &other;
    str->clear();
    f1(*str);
}
