/* Build contract only. A later dialect override must fail compilation. */
#ifndef NN2_C11_GUARD_H
#define NN2_C11_GUARD_H
#if !defined(__STDC_VERSION__) || __STDC_VERSION__ != 201112L
#error "NN2 inner layers require exactly ISO C11"
#endif
#ifndef __STRICT_ANSI__
#error "NN2 inner layers require GNU extensions disabled"
#endif
#endif
