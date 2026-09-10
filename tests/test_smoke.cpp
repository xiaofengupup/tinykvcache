#include "test_utils.h"

#include <iostream>

int main()
{
    TINYKV_CHECK(1 + 1 == 2);

    std::cout << "smoke test passed" << std::endl;
    return 0;
}