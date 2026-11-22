#pragma once
// Domain types (ADR-0004). Plain data, no Windows dependencies.
#include <string>
#include <vector>

#include "core/base.h"
#include "core/date.h"

namespace fp {

// Icon ids are stored in the database, so values must never change. New icons go to the end.
enum class Icon : int {
    None = 0,
    // interface
    Plus, Minus, Close, Check, ChevLeft, ChevRight, ChevDown, ChevUp, Search, Lock, Unlock, Sliders, Grid, List,
    Wallet, Tag, Pie, Bars, Calendar, Trash, Pencil, Eye, EyeOff, Upload, Download, ArrowUp, ArrowDown, Swap, Dots,
    WinMin, WinMax, WinRestore, WinClose, Shield, Pulse, Info, Folder, Key, Clock, Archive, ArrowLeft, Monitor,
    // categories and accounts
    Cart, Food, Coffee, Bus, Car, Home, Bolt, Phone, Wifi, Heart, Shirt, Film, Dumbbell, Book, Gift, Plane, Paw,
    Repeat, Briefcase, Building, Percent, Receipt, Box, Users, User, Wrench, Fuel, Laptop, Bank, Coins, Hammer,
    Truck, Megaphone, Music, Globe, Cash, Card, Star, Leaf, Sparkle, Target, Bed, Pill, Baby,
    Count
};

enum class BookKind : int { Personal = 0, Wallet, Business, Project, Other };
enum class AccountType : int { Cash = 0, Card, Bank, Savings, Credit, Debt };
enum class CatType : int { Expense = 0, Income = 1 };
enum class TxKind : int { Expense = 0, Income = 1, Transfer = 2, Adjustment = 3 };

struct Account {
    i64 id = 0;
    std::wstring name;
    AccountType type = AccountType::Cash;
    i64 opening = 0;     // minor units
    int color = 0;       // palette index
    Icon icon = Icon::Wallet;
    bool archived = false;
    int sort = 0;
    i64 balance = 0;     // computed: opening + lines
};

struct Category {
    i64 id = 0;
    CatType type = CatType::Expense;
    i64 parent = 0;      // 0 = top level
    std::wstring name;
    Icon icon = Icon::Tag;
    int color = 0;
    bool archived = false;
    int sort = 0;
};

struct Line {
    i64 id = 0;
    i64 account = 0;
    i64 category = 0;    // 0 for transfers and adjustments
    i64 amount = 0;      // signed: expense < 0, income > 0
};

struct Transaction {
    i64 id = 0;
    TxKind kind = TxKind::Expense;
    Date date;
    std::wstring payee;
    std::wstring note;
    std::vector<Line> lines;
    i64 deletedAt = 0;

    // Helpers for the single-category editor (MVP UI). Splits are supported by the schema.
    i64 absAmount() const {
        i64 s = 0;
        for (auto& l : lines) s += l.amount < 0 ? -l.amount : l.amount;
        return kind == TxKind::Transfer ? s / 2 : s;
    }
    i64 signedAmount() const {
        i64 s = 0;
        for (auto& l : lines) s += l.amount;
        return s;
    }
    i64 category() const { return lines.empty() ? 0 : lines.front().category; }
    i64 account() const {
        if (kind == TxKind::Transfer)
            for (auto& l : lines) if (l.amount < 0) return l.account;
        return lines.empty() ? 0 : lines.front().account;
    }
    i64 toAccount() const {
        for (auto& l : lines) if (l.amount > 0) return l.account;
        return 0;
    }
};

struct Totals {
    i64 income = 0;
    i64 expense = 0;   // positive number
    i64 net() const { return income - expense; }
};

struct CategorySum {
    i64 category = 0;  // top-level category id
    i64 amount = 0;    // positive
    int count = 0;
};

struct Bucket {
    Date start;        // first day of the bucket
    i64 income = 0;
    i64 expense = 0;
};

enum class BucketSize { Day, Month };

}  // namespace fp
