#pragma once
#include <random>
#include <vector>

#include "../Models/Move.h"
#include "Board.h"
#include "Config.h"

const int INF = 1e9;     // «Бесконечность» для оценочной функции и альфа-бета отсечения

class Logic
{
  public:
      /**
     *  Конструктор — инициализирует движок логики.
     *
     *  board  Указатель на объект доски (для получения матрицы и выполнения ходов).
     *  config Указатель на конфигурацию (настройки бота: уровень, тип оценки, оптимизация).
     *
     * Также инициализирует генератор случайных чисел:
     * - если в конфиге NoRandom = true — сид 0 (детерминированный режим, удобно для тестов);
     * - иначе — сид от текущего времени (случайность каждый запуск).
     */
    Logic(Board *board, Config *config) : board(board), config(config)
    {
        rand_eng = std::default_random_engine (
            !((*config)("Bot", "NoRandom")) ? unsigned(time(0)) : 0);
        scoring_mode = (*config)("Bot", "BotScoringType");
        optimization = (*config)("Bot", "Optimization");
    }
    /**
     * find_best_turns — находит лучшую последовательность ходов для бота.
     *
     * Запускает рекурсивный поиск через find_first_best_turn, затем восстанавливает
     * цепочку ходов по сохранённым состояниям (next_best_state, next_move).
     *
     * color Цвет бота (0 — белые, 1 — чёрные).
     * return Вектор ходов (может содержать серию взятий).
     */
    vector<move_pos> find_best_turns(const bool color)
    {
        
        next_best_state.clear();
        next_move.clear();

        find_first_best_turn(board->get_board(), color, -1, -1, 0);

        int cur_state = 0;     // Восстанавливаем цепочку лучших ходов из сохранённых состояний
        vector<move_pos> res;
        do
        {
            res.push_back(next_move[cur_state]);
            cur_state = next_best_state[cur_state];
        } while (cur_state != -1 && next_move[cur_state].x != -1);
        return res;
        
    }

private:
    /**
     *  make_turn — применяет ход к копии матрицы доски (без изменения оригинала).
     *
     * Используется при поиске в дереве игры: создаёт гипотетическое состояние доски
     * после хода, не затрагивая реальную доску Board.
     *
     * Логика:
     * 1. Если есть взятие — удаляем побитую фигуру.
     * 2. Если шашка достигает последнего ряда — превращаем в дамку (+2 к значению).
     * 3. Перемещаем фигуру в новую клетку, очищаем исходную.
     *
     *  mtx  Копия матрицы доски.
     *  turn Описание хода.
     */
    vector<vector<POS_T>> make_turn(vector<vector<POS_T>> mtx, move_pos turn) const
    {
        if (turn.xb != -1)
            mtx[turn.xb][turn.yb] = 0;     // Удаляем побитую фигуру
        if ((mtx[turn.x][turn.y] == 1 && turn.x2 == 0) || (mtx[turn.x][turn.y] == 2 && turn.x2 == 7))   // Превращение в дамку: белые (1) доходят до ряда 0, чёрные (2) — до ряда 7
            mtx[turn.x][turn.y] += 2;
        mtx[turn.x2][turn.y2] = mtx[turn.x][turn.y];   // Перемещаем фигуру и освобождаем исходную клетку
        mtx[turn.x][turn.y] = 0;
        return mtx;
    }
    /**
     * calc_score — оценочная функция позиции.
     *
     * Оценивает текущее состояние доски с точки зрения бота:
     * чем больше значение, тем выгоднее позиция для бота.
     *
     * mtx Матрица доски.
     * first_bot_color Цвет бота, который ходит первым (true — чёрные, false — белые).
     *                        Определяет, чьи фигуры считаются «своими», а чьи — «чужими».
     * return Оценка позиции (отношение силы противника к силе бота).
     */
    double calc_score(const vector<vector<POS_T>> &mtx, const bool first_bot_color) const
    {
        // color - who is max player
        double w = 0, wq = 0, b = 0, bq = 0;
        for (POS_T i = 0; i < 8; ++i)
        {
            for (POS_T j = 0; j < 8; ++j)
            {
                w += (mtx[i][j] == 1);   // Подсчёт белых шашек
                wq += (mtx[i][j] == 3);  // Подсчёт белых дамок
                b += (mtx[i][j] == 2);   // Подсчёт чёрных шашек
                bq += (mtx[i][j] == 4);  // Подсчёт чёрных дамок 
                // В режиме "NumberAndPotential" учитываем позиционный потенциал:             ||
                // белые шашки тем ценнее, чем ближе к ряду 0 (ближе к превращению в дамку);  || 
                // чёрные — чем ближе к ряду 7                                                \/
                if (scoring_mode == "NumberAndPotential")
                {
                    w += 0.05 * (mtx[i][j] == 1) * (7 - i);
                    b += 0.05 * (mtx[i][j] == 2) * (i);
                }
            }
        }
        if (!first_bot_color)  // Если первый бот — белые, меняем местами счётчики, чтобы формула ниже всегда считала b/bq — «свои», w/wq — «чужие»
        {
            swap(b, w);
            swap(bq, wq);
        }
        if (w + wq == 0)     // Если у бота не осталось фигур — худшая позиция
            return INF;
        if (b + bq == 0)     // Если у противника не осталось фигур — лучшая позиция
            return 0;
        int q_coef = 4;      // Коэффициент ценности дамки относительно шашки
        if (scoring_mode == "NumberAndPotential")
        {
            q_coef = 5;     // В режиме с потенциалом дамка ценится выше
        }
        return (b + bq * q_coef) / (w + wq * q_coef);     // Итоговая оценка: отношение силы «своих» к силе «чужих»
    }

    double find_first_best_turn(vector<vector<POS_T>> mtx, const bool color, const POS_T x, const POS_T y, size_t state,
                                double alpha = -1)

        /**
     * find_first_best_turn — рекурсивный поиск лучшего хода для текущего бота
     *        с учётом серий взятий (обрабатывает цепочки взятий одним вызовом).
     *
     * mtx    Текущая матрица доски.
     * color  Цвет текущего игрока.
     * x, y   Координаты фигуры, продолжающей серию взятий (-1, -1 для первого хода).
     * state  Индекс текущего состояния в массивах next_move / next_best_state.
     * alpha  Текущее лучшее значение для альфа-бета отсечения.
     * Оценка лучшей найденной позиции.
     */

    {
        next_best_state.push_back(-1);
        next_move.emplace_back(-1, -1, -1, -1);
        double best_score = -1;
        if (state != 0)                     // Если это не первый ход (state != 0), ищем ходы только для конкретной фигуры (продолжение серии)
            find_turns(x, y, mtx);
        auto turns_now = turns;
        bool have_beats_now = have_beats;

        if (!have_beats_now && state != 0)  // Если продолжать взятие нельзя — передаём ход противнику через рекурсивный поиск
        {
            return find_best_turns_rec(mtx, 1 - color, 0, alpha);
        }

        vector<move_pos> best_moves;
        vector<int> best_states;

        for (auto turn : turns_now)         // Перебираем все возможные ходы из текущей позиции
        {
            size_t next_state = next_move.size();
            double score;
            if (have_beats_now)  
            {
                score = find_first_best_turn(make_turn(mtx, turn), color, turn.x2, turn.y2, next_state, best_score); // Взятие: продолжаем искать ходы для той же фигуры (серия взятий)
            }
            else
            {
                score = find_best_turns_rec(make_turn(mtx, turn), 1 - color, 0, best_score);  // Обычный ход: передаём ход противнику
            }
            if (score > best_score)     // Обновляем лучший ход, если нашли более выгодный
            {
                best_score = score;
                next_best_state[state] = (have_beats_now ? int(next_state) : -1);
                next_move[state] = turn;
            }
        }
        return best_score;
    }


    /**
     * find_best_turns_rec — рекурсивный минимакс с альфа-бета отсечением.
     *
     * Чередует максимизацию (ход бота) и минимизацию (ход противника).
     * Глубина ограничена Max_depth из конфига.
     *
     * mtx    Матрица доски.
     * color  Цвет текущего игрока.
     * depth  Текущая глубина поиска (0 — корень).
     * alpha  Лучший score для максимизирующего игрока.
     * beta   Лучший score для минимизирующего игрока.
     * x, y   Координаты фигуры, продолжающей серию взятий (-1 если не требуется).
     * Оценка позиции.
     */

    double find_best_turns_rec(vector<vector<POS_T>> mtx, const bool color, const size_t depth, double alpha = -1,
                               double beta = INF + 1, const POS_T x = -1, const POS_T y = -1)
    {
        if (depth == Max_depth)      // Достигнута максимальная глубина — оцениваем лист
        {
            return calc_score(mtx, (depth % 2 == color));
        }
        if (x != -1)  // Если задана конкретная клетка — ищем ходы только для неё
        {
            find_turns(x, y, mtx);
        }
        else
            find_turns(color, mtx);
        auto turns_now = turns;
        bool have_beats_now = have_beats;

        if (!have_beats_now && x != -1)   // Если взятий нет и мы были в середине серии — передаём ход противнику
        {
            return find_best_turns_rec(mtx, 1 - color, depth + 1, alpha, beta);
        }

        if (turns.empty())     // Нет ходов вообще — проигрыш текущего игрока
            return (depth % 2 ? 0 : INF);

        double min_score = INF + 1;   // Для минимизирующего уровня
        double max_score = -1;        // Для максимизирующего уровня
        for (auto turn : turns_now)
        {
            double score = 0.0;
            if (!have_beats_now && x == -1)
            {
                score = find_best_turns_rec(make_turn(mtx, turn), 1 - color, depth + 1, alpha, beta);  // Обычный ход — передаём ход противнику, увеличиваем глубину
            }
            else
            {
                score = find_best_turns_rec(make_turn(mtx, turn), color, depth, alpha, beta, turn.x2, turn.y2); // Взятие — тот же игрок продолжает серию, глубина не меняется
            }
            min_score = min(min_score, score);
            max_score = max(max_score, score);
            // alpha-beta pruning
            if (depth % 2)                       // Альфа-бета отсечение: отсекаем ветви, которые не могут улучшить результат
                alpha = max(alpha, max_score);
            else
                beta = min(beta, min_score);
            if (optimization != "O0" && alpha >= beta)
                return (depth % 2 ? max_score + 1 : min_score - 1);
        }
        return (depth % 2 ? max_score : min_score);
    }

public:     // Публичные перегрузки find_turns: работают с реальной доской
    void find_turns(const bool color)
    {
        find_turns(color, board->get_board());
    }


    /**
     * find_turns (перегрузка 2) — находит все возможные ходы для конкретной
     * фигуры на текущей доске (board). Используется при обработке серии взятий,
     * когда нужно проверить, может ли конкретная фигура продолжить бить
     *
     * Координата X (строка) фигуры
     * Координата Y (столбец) фигуры
     */

    void find_turns(const POS_T x, const POS_T y)
    {
        find_turns(x, y, board->get_board());
    }

private:    // Приватные перегрузки find_turns: работают с произвольной матрицей

    /**
     * find_turns (перегрузка 3) — находит все ходы для указанного цвета
     * на заданной матрице доски. Если есть хотя бы одно взятие —
     * остаются только ходы-взятия (правило обязательного битья)
     *
     * Логика:
     * 1 Перебираем все клетки, ищем фигуры нужного цвета
     * 2 Для каждой вызываем find_turns(x, y, mtx)
     * 3 Если найдено взятие, а раньше взятий не было — очищаем список и начинаем заново
     * 4 В конце перемешиваем ходы (для разнообразия партий бота)
     *
     * color Цвет игрока
     * mtx Матрица доски
     */

    void find_turns(const bool color, const vector<vector<POS_T>> &mtx)
    {
        vector<move_pos> res_turns;
        bool have_beats_before = false;    // Флаг: было ли найдено хотя бы одно взятие
        for (POS_T i = 0; i < 8; ++i)
        {
            for (POS_T j = 0; j < 8; ++j)
            {
                if (mtx[i][j] && mtx[i][j] % 2 != color)
                {
                    find_turns(i, j, mtx);
                    if (have_beats && !have_beats_before)     // Первое найденное взятие — сбрасываем накопленные обычные ходы
                    {
                        have_beats_before = true;
                        res_turns.clear();
                    }
                    if ((have_beats_before && have_beats) || !have_beats_before)   // Добавляем ходы: взятия (если уже были взятия) или все (если взятий нет)
                    {
                        res_turns.insert(res_turns.end(), turns.begin(), turns.end());
                    }
                }
            }
        }
        turns = res_turns;
        shuffle(turns.begin(), turns.end(), rand_eng);     // Перемешиваем для случайности
        have_beats = have_beats_before;
    }

    void find_turns(const POS_T x, const POS_T y, const vector<vector<POS_T>> &mtx)
    {
        turns.clear();
        have_beats = false;
        POS_T type = mtx[x][y];     // Тип фигуры: 1 — б.шашка, 2 — ч.шашка, 3 — б.дамка, 4 — ч.дамка
        // check beats
        switch (type)     // Поиск взятий
        {
        case 1:
        case 2:
            // check pieces
            for (POS_T i = x - 2; i <= x + 2; i += 4)   // Для обычных шашек: взятие — прыжок на 2 клетки по диагонали
                                                       // Проверяем все 4 диагональных направления (через (-2/+2) и (-2/+2))
            {
                for (POS_T j = y - 2; j <= y + 2; j += 4)
                {
                    if (i < 0 || i > 7 || j < 0 || j > 7)
                        continue;
                    POS_T xb = (x + i) / 2, yb = (y + j) / 2;                      // Координаты побитой фигуры
                    if (mtx[i][j] || !mtx[xb][yb] || mtx[xb][yb] % 2 == type % 2)  // Пропуск, если: клетка назначения занята, бить нечего, или фигура своего цвета
                        continue;
                    turns.emplace_back(x, y, i, j, xb, yb);
                }
            }
            break;
        default:
            // check queens
            // Для дамок: взятие — прыжок через фигуру противника на любую дальность по диагонали
            // Дамка бьёт по 4 диагоналям, может «пролететь» любое число пустых клеток до жертвы и после
            for (POS_T i = -1; i <= 1; i += 2)
            {
                for (POS_T j = -1; j <= 1; j += 2)
                {
                    POS_T xb = -1, yb = -1;    // Координаты фигуры, которую бьём (ищем по ходу движения)
                    for (POS_T i2 = x + i, j2 = y + j; i2 != 8 && j2 != 8 && i2 != -1 && j2 != -1; i2 += i, j2 += j) // Движемся по диагонали, пока не выйдем за пределы доски
                    {
                        if (mtx[i2][j2])
                        {  
                            // Встретили фигуру.
                            // Если своя — стоп (бить нельзя).
                            // Если чужая, но уже нашли одну — тоже стоп (нельзя бить две за раз)

                            if (mtx[i2][j2] % 2 == type % 2 || (mtx[i2][j2] % 2 != type % 2 && xb != -1))
                            {
                                break;
                            }
                            xb = i2;  // Запоминаем координаты фигуры для взятия
                            yb = j2;
                        }
                        if (xb != -1 && xb != i2)   // Если нашли фигуру для взятия и прошли за неё — добавляем ход
                        {
                            turns.emplace_back(x, y, i2, j2, xb, yb);
                        }
                    }
                }
            }
            break;
        }
        // check other turns
        if (!turns.empty())     // Если взятия есть — возвращаем только их
        {
            have_beats = true;
            return;
        }
        switch (type)           // Поиск обычных ходов
        {
        case 1:
        case 2:
            // check pieces
            // Для шашек: ход на 1 клетку по диагонали вперёд
            // Белые (type % 2 == 1) двигаются вверх (x - 1), чёрные (type % 2 == 0) — вниз (x + 1)
            {
                POS_T i = ((type % 2) ? x - 1 : x + 1);
                for (POS_T j = y - 1; j <= y + 1; j += 2)
                {
                    if (i < 0 || i > 7 || j < 0 || j > 7 || mtx[i][j])
                        continue;
                    turns.emplace_back(x, y, i, j);
                }
                break;
            }
        default:
            // check queens
            // Для дамок: ход на любое количество клеток по диагонали, пока путь свободен
            for (POS_T i = -1; i <= 1; i += 2)
            {
                for (POS_T j = -1; j <= 1; j += 2)
                {
                    for (POS_T i2 = x + i, j2 = y + j; i2 != 8 && j2 != 8 && i2 != -1 && j2 != -1; i2 += i, j2 += j)
                    {
                        if (mtx[i2][j2])
                            break;   // Путь преграждён
                        turns.emplace_back(x, y, i2, j2);
                    }
                }
            }
            break;
        }
    }

  public:
    vector<move_pos> turns;   // Список найденных ходов для текущей позиции
    bool have_beats;          // Флаг: есть ли взятия среди найденных ходов
    int Max_depth;            // Максимальная глубина поиска бота

  private:
    default_random_engine rand_eng;   // Генератор случайных чисел для перемешивания ходов
    string scoring_mode;              // Режим оценочной функции из конфига
    string optimization;              // Режим оптимизации из конфига
    vector<move_pos> next_move;       // Массив лучших ходов для каждого состояния в дереве поиска
    vector<int> next_best_state;      // Массив индексов следующих состояний
    Board *board;                     // Указатель на объект доски — для получения матрицы и выполнения ходов
    Config *config;                   // Указатель на конфигурацию — для чтения настроек бота
};
