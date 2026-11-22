#pragma once
// ADR-0015: English and Russian, compiled into the exe. Every string has both translations by construction.
#include <string>
#include <vector>

#include "core/date.h"
#include "core/model.h"
#include "storage/book.h"

namespace fp {

#define FP_STRINGS(X)                                                                                              \
    X(AppName, L"Finpulse", L"Finpulse")                                                                           \
    X(Ok, L"OK", L"OK")                                                                                            \
    X(Cancel, L"Cancel", L"Отмена")                                                                                \
    X(Save, L"Save", L"Сохранить")                                                                                 \
    X(Delete, L"Delete", L"Удалить")                                                                               \
    X(Edit, L"Edit", L"Изменить")                                                                                  \
    X(Add, L"Add", L"Добавить")                                                                                    \
    X(Close, L"Close", L"Закрыть")                                                                                 \
    X(Back, L"Back", L"Назад")                                                                                     \
    X(Create, L"Create", L"Создать")                                                                               \
    X(Continue, L"Continue", L"Продолжить")                                                                        \
    X(Search, L"Search", L"Поиск")                                                                                 \
    X(Today, L"Today", L"Сегодня")                                                                                 \
    X(Yesterday, L"Yesterday", L"Вчера")                                                                           \
    X(Undo, L"Undo", L"Отменить")                                                                                  \
    X(Archive, L"Archive", L"В архив")                                                                             \
    X(Unarchive, L"Restore from archive", L"Вернуть из архива")                                                   \
    X(Archived, L"Archived", L"В архиве")                                                                          \
    X(Restore, L"Restore", L"Восстановить")                                                                        \
    X(Name, L"Name", L"Название")                                                                                  \
    X(Color, L"Color", L"Цвет")                                                                                    \
    X(IconLabel, L"Icon", L"Иконка")                                                                               \
    X(Type, L"Type", L"Тип")                                                                                       \
    X(Currency, L"Currency", L"Валюта")                                                                            \
    /* start screen */                                                                                             \
    X(StartTitle, L"Books", L"Книги")                                                                              \
    X(StartSubtitle, L"Personal money, wallets and businesses. Everything stays on this computer.",                \
      L"Личные деньги, кошельки и предприятия. Всё хранится только на этом компьютере.")                            \
    X(NewBook, L"New book", L"Новая книга")                                                                        \
    X(Import, L"Import", L"Импорт")                                                                                \
    X(Export, L"Export", L"Экспорт")                                                                               \
    X(Settings, L"Settings", L"Настройки")                                                                         \
    X(Locked, L"Locked", L"Заблокирована")                                                                         \
    X(EmptyBooksTitle, L"Create your first book", L"Создайте первую книгу")                                        \
    X(EmptyBooksHint, L"A book is a separate ledger: personal money, a wallet or a business. Each has its own currency.", \
      L"Книга это отдельный учёт: личные деньги, кошелёк или предприятие. У каждой своя валюта.")                   \
    X(OpOne, L"transaction", L"операция")                                                                          \
    X(OpFew, L"transactions", L"операции")                                                                         \
    X(OpMany, L"transactions", L"операций")                                                                        \
    X(AccOne, L"account", L"счёт")                                                                                 \
    X(AccFew, L"accounts", L"счёта")                                                                               \
    X(AccMany, L"accounts", L"счетов")                                                                             \
    X(KindPersonal, L"Personal", L"Личное")                                                                        \
    X(KindWallet, L"Wallet", L"Кошелёк")                                                                           \
    X(KindBusiness, L"Business", L"Предприятие")                                                                   \
    X(KindProject, L"Project", L"Проект")                                                                          \
    X(KindOther, L"Other", L"Другое")                                                                              \
    X(NewBookTitle, L"New book", L"Новая книга")                                                                   \
    X(EditBookTitle, L"Book", L"Книга")                                                                            \
    X(BookNamePlaceholder, L"For example, Personal or My company", L"Например, Личное или Моя фирма")            \
    X(CurrencyFixedHint, L"All amounts in the book use this currency.", L"Все суммы книги ведутся в этой валюте.")  \
    /* PIN */                                                                                                      \
    X(EnterPin, L"Enter PIN", L"Введите PIN")                                                                      \
    X(PinWrong, L"Wrong PIN", L"Неверный PIN")                                                                     \
    X(PinWait, L"Too many attempts. Try again in", L"Слишком много попыток. Повторите через")                      \
    X(Seconds, L"s", L"с")                                                                                         \
    X(Pin, L"PIN", L"PIN")                                                                                         \
    X(SetPin, L"Set PIN", L"Установить PIN")                                                                       \
    X(ChangePin, L"Change PIN", L"Сменить PIN")                                                                    \
    X(RemovePin, L"Remove PIN", L"Снять PIN")                                                                      \
    X(PinNew, L"New PIN", L"Новый PIN")                                                                            \
    X(PinRepeat, L"Repeat PIN", L"Повторите PIN")                                                                  \
    X(PinCurrent, L"Current PIN", L"Текущий PIN")                                                                  \
    X(PinMismatch, L"PINs do not match", L"PIN не совпадает")                                                      \
    X(PinOn, L"PIN is on", L"PIN включён")                                                                         \
    X(PinOff, L"No PIN", L"Без PIN")                                                                               \
    X(PinHint, L"4 digits. Keeps the book closed for anyone who sits at your computer.",                            \
      L"4 цифры. Не даёт открыть книгу тому, кто сел за ваш компьютер.")                                           \
    X(PinForExport, L"Enter the PIN of this book to include it in the export",                                     \
      L"Введите PIN этой книги, чтобы включить её в экспорт")                                                      \
    X(Skip, L"Skip", L"Пропустить")                                                                                \
    /* navigation */                                                                                               \
    X(NavOverview, L"Overview", L"Обзор")                                                                          \
    X(NavTransactions, L"Transactions", L"Операции")                                                               \
    X(NavAccounts, L"Accounts", L"Счета")                                                                          \
    X(NavCategories, L"Categories", L"Категории")                                                                  \
    X(NavReports, L"Reports", L"Отчёты")                                                                           \
    X(NavBookSettings, L"Book settings", L"Настройки книги")                                                       \
    X(AllBooks, L"All books", L"Все книги")                                                                        \
    X(LockBook, L"Lock", L"Заблокировать")                                                                         \
    /* periods */                                                                                                  \
    X(PeriodDay, L"Day", L"День")                                                                                  \
    X(PeriodWeek, L"Week", L"Неделя")                                                                              \
    X(PeriodMonth, L"Month", L"Месяц")                                                                             \
    X(PeriodQuarter, L"Quarter", L"Квартал")                                                                       \
    X(PeriodYear, L"Year", L"Год")                                                                                 \
    X(PeriodAll, L"All time", L"Всё время")                                                                        \
    X(QuarterShort, L"Q", L"Кв.")                                                                                  \
    /* overview */                                                                                                 \
    X(Balance, L"Balance", L"Баланс")                                                                              \
    X(Income, L"Income", L"Доходы")                                                                                \
    X(Expenses, L"Expenses", L"Расходы")                                                                           \
    X(Net, L"Result", L"Итог")                                                                                     \
    X(ExpensesByCategory, L"Expenses by category", L"Расходы по категориям")                                       \
    X(IncomeByCategory, L"Income by category", L"Доходы по категориям")                                            \
    X(CashFlow, L"Cash flow", L"Денежный поток")                                                                   \
    X(Recent, L"Recent transactions", L"Последние операции")                                                       \
    X(ShowAll, L"Show all", L"Показать все")                                                                       \
    X(NoTransactions, L"No transactions yet", L"Операций пока нет")                                                \
    X(NoTransactionsHint, L"Press Ctrl+N or the + button to add the first one.",                                   \
      L"Нажмите Ctrl+N или кнопку + чтобы добавить первую.")                                                       \
    X(NoData, L"No data for this period", L"Нет данных за этот период")                                            \
    X(VsPrevious, L"vs previous", L"к прошлому")                                                                   \
    X(SavingsRate, L"Saved", L"Сбережено")                                                                         \
    X(OtherCategories, L"Other", L"Прочее")                                                                        \
    /* transactions */                                                                                             \
    X(Expense, L"Expense", L"Расход")                                                                              \
    X(IncomeOne, L"Income", L"Доход")                                                                              \
    X(Transfer, L"Transfer", L"Перевод")                                                                           \
    X(Adjustment, L"Balance correction", L"Корректировка")                                                         \
    X(NewTransaction, L"New transaction", L"Новая операция")                                                       \
    X(EditTransaction, L"Transaction", L"Операция")                                                                \
    X(Amount, L"Amount", L"Сумма")                                                                                 \
    X(Category, L"Category", L"Категория")                                                                         \
    X(Account, L"Account", L"Счёт")                                                                                \
    X(FromAccount, L"From", L"Откуда")                                                                             \
    X(ToAccount, L"To", L"Куда")                                                                                   \
    X(DateLabel, L"Date", L"Дата")                                                                                 \
    X(Payee, L"Payee", L"Получатель")                                                                              \
    X(PayeePlaceholder, L"Shop, person or client", L"Магазин, человек или клиент")                                \
    X(Note, L"Note", L"Заметка")                                                                                   \
    X(NotePlaceholder, L"Optional comment", L"Комментарий, если нужен")                                           \
    X(AmountHint, L"You can type 120+35 or 900/3", L"Можно ввести 120+35 или 900/3")                               \
    X(ErrAmount, L"Enter an amount greater than zero", L"Введите сумму больше нуля")                               \
    X(ErrCategory, L"Choose a category", L"Выберите категорию")                                                    \
    X(ErrSameAccount, L"Choose two different accounts", L"Выберите два разных счёта")                              \
    X(ErrName, L"Enter a name", L"Введите название")                                                               \
    X(TxDeleted, L"Transaction deleted", L"Операция удалена")                                                      \
    X(TxSaved, L"Saved", L"Сохранено")                                                                             \
    X(SaveAndNext, L"Save and add next", L"Сохранить и ещё")                                                      \
    X(SearchPlaceholder, L"Search payee, note or category", L"Поиск по получателю, заметке, категории")            \
    X(Trash, L"Trash", L"Корзина")                                                                                 \
    X(TrashHint, L"Deleted transactions are kept for 30 days.", L"Удалённые операции хранятся 30 дней.")          \
    X(TrashEmpty, L"Trash is empty", L"Корзина пуста")                                                             \
    X(DeleteForever, L"Delete forever", L"Удалить навсегда")                                                       \
    X(PickDate, L"Pick a date", L"Выбрать дату")                                                                   \
    /* accounts */                                                                                                 \
    X(NewAccount, L"New account", L"Новый счёт")                                                                   \
    X(EditAccount, L"Account", L"Счёт")                                                                            \
    X(OpeningBalance, L"Starting balance", L"Начальный остаток")                                                   \
    X(AccCash, L"Cash", L"Наличные")                                                                               \
    X(AccCard, L"Card", L"Карта")                                                                                  \
    X(AccBank, L"Bank account", L"Банковский счёт")                                                                \
    X(AccSavings, L"Savings", L"Сбережения")                                                                       \
    X(AccCredit, L"Credit", L"Кредит")                                                                             \
    X(AccDebt, L"Debt", L"Долг")                                                                                   \
    X(DefaultAccountName, L"Cash", L"Наличные")                                                                    \
    X(TotalBalance, L"Total balance", L"Общий баланс")                                                             \
    X(CorrectBalance, L"Correct balance", L"Скорректировать остаток")                                              \
    X(ActualBalance, L"Actual balance now", L"Фактический остаток сейчас")                                         \
    X(ShowArchived, L"Show archived", L"Показать архив")                                                           \
    /* categories */                                                                                               \
    X(NewCategory, L"New category", L"Новая категория")                                                            \
    X(EditCategory, L"Category", L"Категория")                                                                     \
    X(ParentCategory, L"Inside category", L"Внутри категории")                                                     \
    X(TopLevel, L"Top level", L"Верхний уровень")                                                                  \
    X(MergeInto, L"Merge into", L"Объединить с")                                                                   \
    X(MergeHint, L"All transactions move to the chosen category, this one is removed.",                            \
      L"Все операции перейдут в выбранную категорию, эта будет удалена.")                                          \
    X(Uses, L"uses", L"исп.")                                                                                      \
    /* reports */                                                                                                  \
    X(Reports, L"Reports", L"Отчёты")                                                                              \
    X(Monthly, L"By month", L"По месяцам")                                                                         \
    X(Daily, L"By day", L"По дням")                                                                                \
    X(AveragePerDay, L"Average per day", L"В среднем за день")                                                     \
    X(AveragePerMonth, L"Average per month", L"В среднем за месяц")                                                \
    X(Largest, L"Largest category", L"Самая крупная категория")                                                    \
    X(Share, L"Share", L"Доля")                                                                                    \
    /* settings */                                                                                                 \
    X(AppSettings, L"Settings", L"Настройки")                                                                      \
    X(Language, L"Language", L"Язык")                                                                              \
    X(Security, L"Security", L"Безопасность")                                                                      \
    X(AutoLock, L"Lock books after inactivity", L"Блокировать книги при бездействии")                              \
    X(Minutes, L"min", L"мин")                                                                                     \
    X(Never, L"Never", L"Никогда")                                                                                 \
    X(LockOnMinimize, L"Lock when the window is minimized", L"Блокировать при сворачивании окна")                  \
    X(HideFromCapture, L"Hide the window from screenshots and screen sharing",                                     \
      L"Скрывать окно от скриншотов и демонстрации экрана")                                                        \
    X(PrivacyMode, L"Hide amounts", L"Скрыть суммы")                                                               \
    X(Display, L"Display", L"Отображение")                                                                         \
    X(FirstDayOfWeek, L"First day of week", L"Первый день недели")                                                 \
    X(Monday, L"Monday", L"Понедельник")                                                                           \
    X(Sunday, L"Sunday", L"Воскресенье")                                                                           \
    X(Data, L"Data", L"Данные")                                                                                    \
    X(DataFolder, L"Data folder", L"Папка данных")                                                                 \
    X(ExportAll, L"Export all data", L"Экспорт всех данных")                                                       \
    X(ImportData, L"Import data", L"Импорт данных")                                                                \
    X(AutoBackup, L"Automatic backup on exit", L"Автоматический бэкап при выходе")                                 \
    X(OpenFolder, L"Open folder", L"Открыть папку")                                                                \
    X(Shortcuts, L"Shortcuts", L"Ярлыки")                                                                          \
    X(DesktopShortcut, L"Desktop shortcut", L"Ярлык на рабочем столе")                                             \
    X(StartMenuShortcut, L"Start menu shortcut", L"Ярлык в меню Пуск")                                             \
    X(ShortcutCreated, L"Shortcut created", L"Ярлык создан")                                                       \
    X(About, L"About", L"О программе")                                                                             \
    X(OfflineNote, L"Finpulse works fully offline and never connects to the internet.",                            \
      L"Finpulse работает полностью офлайн и никогда не выходит в интернет.")                                      \
    X(Version, L"Version", L"Версия")                                                                              \
    X(DeleteBook, L"Delete book", L"Удалить книгу")                                                                \
    X(DeleteBookConfirm, L"The book and all its transactions will be deleted permanently.",                        \
      L"Книга и все её операции будут удалены навсегда.")                                                          \
    X(DeleteConfirmTitle, L"Delete?", L"Удалить?")                                                                 \
    /* export / import */                                                                                          \
    X(ExportTitle, L"Export all data", L"Экспорт всех данных")                                                     \
    X(ExportHint, L"One file with every book and all settings. Use it to move to another computer or to reinstall.", \
      L"Один файл со всеми книгами и настройками. Для переезда на другой компьютер или переустановки.")             \
    X(BackupPassword, L"Password for the file (optional)", L"Пароль на файл (по желанию)")                         \
    X(BackupPasswordRepeat, L"Repeat password", L"Повторите пароль")                                               \
    X(PasswordMismatch, L"Passwords do not match", L"Пароли не совпадают")                                         \
    X(NoPasswordWarn, L"Without a password anyone who gets this file can read your data.",                         \
      L"Без пароля любой, кто получит файл, сможет прочитать данные.")                                             \
    X(ExportDone, L"Export finished", L"Экспорт завершён")                                                         \
    X(ImportTitle, L"Import data", L"Импорт данных")                                                               \
    X(ImportPasswordPrompt, L"This file is protected with a password", L"Файл защищён паролем")                    \
    X(ImportModeAdd, L"Add books to the current ones", L"Добавить книги к текущим")                                \
    X(ImportModeReplace, L"Replace all data with the file", L"Заменить все данные содержимым файла")              \
    X(ImportResetPins, L"Remove PINs from imported books", L"Снять PIN с импортируемых книг")                      \
    X(ImportReplaceWarn, L"Current data will be saved to an automatic backup first.",                              \
      L"Перед заменой текущие данные будут сохранены в автоматический бэкап.")                                     \
    X(ImportDone, L"Import finished", L"Импорт завершён")                                                          \
    X(BooksInFile, L"Books in the file", L"Книг в файле")                                                          \
    X(Created, L"Created", L"Создан")                                                                              \
    X(Password, L"Password", L"Пароль")                                                                            \
    X(Exists, L"already exists, will be kept as a copy", L"уже есть, будет добавлена копией")                      \
    /* errors */                                                                                                   \
    X(ErrIo, L"The file could not be read or written", L"Не удалось прочитать или записать файл")                  \
    X(ErrCorrupt, L"The data is damaged", L"Данные повреждены")                                                    \
    X(ErrWrongPin, L"Wrong PIN", L"Неверный PIN")                                                                  \
    X(ErrThrottled, L"Too many attempts, please wait", L"Слишком много попыток, подождите")                        \
    X(ErrWrongPassword, L"Wrong password", L"Неверный пароль")                                                     \
    X(ErrTooNew, L"This file was made by a newer version of Finpulse", L"Файл создан более новой версией Finpulse") \
    X(ErrNotFound, L"Not found", L"Не найдено")                                                                    \
    X(ErrInvalid, L"Check the entered data", L"Проверьте введённые данные")                                        \
    X(ErrCrypto, L"Windows encryption error", L"Ошибка шифрования Windows")                                        \
    X(DataFolderFallback, L"The folder next to Finpulse.exe is read-only, data is kept in your profile.",          \
      L"Папка рядом с Finpulse.exe недоступна для записи, данные хранятся в профиле.")                             \
    /* default categories */                                                                                       \
    X(CatGroceries, L"Groceries", L"Продукты")                                                                     \
    X(CatCafes, L"Cafes and restaurants", L"Кафе и рестораны")                                                     \
    X(CatTransport, L"Transport", L"Транспорт")                                                                    \
    X(CatCar, L"Car", L"Автомобиль")                                                                               \
    X(CatHousing, L"Housing", L"Жильё")                                                                            \
    X(CatUtilities, L"Utilities", L"Коммунальные")                                                                 \
    X(CatPhone, L"Phone and internet", L"Связь и интернет")                                                        \
    X(CatHealth, L"Health", L"Здоровье")                                                                           \
    X(CatClothes, L"Clothes", L"Одежда")                                                                           \
    X(CatFun, L"Entertainment", L"Развлечения")                                                                    \
    X(CatSport, L"Sport", L"Спорт")                                                                                \
    X(CatEducation, L"Education", L"Образование")                                                                  \
    X(CatGifts, L"Gifts", L"Подарки")                                                                              \
    X(CatTravel, L"Travel", L"Путешествия")                                                                        \
    X(CatPets, L"Pets", L"Питомцы")                                                                                \
    X(CatSubscriptions, L"Subscriptions", L"Подписки")                                                             \
    X(CatOther, L"Other", L"Другое")                                                                               \
    X(CatSalary, L"Salary", L"Зарплата")                                                                           \
    X(CatSideIncome, L"Side income", L"Подработка")                                                                \
    X(CatInterest, L"Interest", L"Проценты")                                                                       \
    X(CatRefunds, L"Refunds", L"Возвраты")                                                                         \
    X(CatMaterials, L"Materials", L"Материалы")                                                                    \
    X(CatSubcontractors, L"Subcontractors", L"Субподрядчики")                                                      \
    X(CatSalaries, L"Salaries", L"Зарплаты")                                                                       \
    X(CatRent, L"Rent", L"Аренда")                                                                                 \
    X(CatTools, L"Tools and equipment", L"Инструменты и оборудование")                                             \
    X(CatFuel, L"Vehicles and fuel", L"Транспорт и топливо")                                                       \
    X(CatTaxes, L"Taxes and fees", L"Налоги и сборы")                                                              \
    X(CatAds, L"Advertising", L"Реклама")                                                                          \
    X(CatSoftware, L"Software and services", L"Программы и сервисы")                                               \
    X(CatOffice, L"Office", L"Офис")                                                                               \
    X(CatBankFees, L"Bank fees", L"Банковские комиссии")                                                           \
    X(CatSales, L"Sales", L"Продажи")                                                                              \
    X(CatServices, L"Services", L"Услуги")                                                                         \
    X(CatProjects, L"Projects", L"Проекты")                                                                        \
    /* shortcuts help */                                                                                           \
    X(KeysTitle, L"Keyboard", L"Клавиатура")                                                                       \
    X(KeysText, L"Ctrl+N expense   Ctrl+I income   Ctrl+T transfer   Ctrl+F search   Ctrl+Z undo   Ctrl+L lock   Alt+Left/Right period", \
      L"Ctrl+N расход   Ctrl+I доход   Ctrl+T перевод   Ctrl+F поиск   Ctrl+Z отмена   Ctrl+L блокировка   Alt+стрелки период")

#define FP_ENUM_ITEM(id, en, ru) id,
enum class S : int { FP_STRINGS(FP_ENUM_ITEM) Count };
#undef FP_ENUM_ITEM

enum class Lang : int { En = 0, Ru = 1 };

void setLang(Lang l);
Lang lang();
Lang systemLang();
const wchar_t* tr(S s);
const wchar_t* trIn(S s, Lang l);

std::wstring plural(long long n, S one, S few, S many);
std::wstring monthName(int m, bool standalone);   // standalone: "Ноябрь"; in-phrase: "ноября"
std::wstring monthShort(int m);
std::wstring weekdayShort(int wd);                // 0 = Monday
std::wstring periodLabel(const Period& p);
std::wstring dateLabel(const Date& d, bool relative = true);
std::wstring dateShort(const Date& d);
std::wstring kindName(BookKind k);
std::wstring accountTypeName(AccountType t);
std::wstring errorText(Ec e);
std::vector<SeedCategory> defaultCategories(BookKind k);

}  // namespace fp
