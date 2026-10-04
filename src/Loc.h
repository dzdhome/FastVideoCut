// ---------------------------------------------------------------------------
// Loc.h - UI language (Simplified Chinese / English)
// ---------------------------------------------------------------------------
// 取值同时用作 INI 里的数字，所以不要改顺序：
//   0 = 跟随系统（首次启动按系统区域自动选）
//   1 = 简体中文
//   2 = English
// ---------------------------------------------------------------------------
#pragma once

#include <string>

enum class AppLang
{
    Auto    = 0,
    Chinese = 1,
    English = 2
};

namespace Loc
{
    // 按当前用户的区域设置判断：zh* -> 简体中文，其余（en/ja/ko...）-> English
    AppLang Detect();

    // 把配置值（可能是 Auto）解析成实际使用的语言
    AppLang Resolve(AppLang configured);

    // 设定当前语言，之后所有 TR() 都按它取文本
    void   Apply(AppLang configured);

    AppLang Configured();   // 用户选的（可能是 Auto）
    AppLang Current();      // 实际生效的
    bool   IsEnglish();

    // 语言下拉框里的名字。中文/英文都用各自语言自称，不随界面语言变化。
    const wchar_t* LangName(AppLang l);

    // 设置里的说明文字，例如“跟随系统（当前：简体中文）”
    std::wstring Describe(AppLang configured);
}

// 翻译宏：把中文原文和英文并排写在一起，运行时二选一。
// 并排放置的好处是两种语言的上下文永远挨着，维护时不容易漏翻；
// 代价是两种文本都会被编进二进制（几十 KB，可以接受）。
#define TR(zh, en) (Loc::IsEnglish() ? (en) : (zh))