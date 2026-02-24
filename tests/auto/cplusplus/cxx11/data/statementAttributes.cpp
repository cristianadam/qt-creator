void func()
{
    int i = 0;
    int j;
    if (i < 0) [[unlikely]]
        i = 0;
    else [[likely]]
        return;
    [[likely]] try {
        [[likely]] for (j = i; j < 100; ++j)
            [[unlikely]];
    } catch (...) {}
    switch (j) {
    [[likely]] case 100: break;
    [[unlikely]] default: return;
    }
}
