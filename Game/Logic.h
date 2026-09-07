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
        // Очищаем массивы для нового поиска.
        // next_move[i] — лучший ход из состояния i.
        // next_best_state[i] — индекс следующего состояния или -1 (переход к противнику).
        next_best_state.clear();
        next_move.clear();

        // Запускаем рекурсивный поиск с корневого состояния 0.
        // Координаты -1, -1 означают, что это первый ход, а не продолжение серии взятий.
        find_first_best_turn(board->get_board(), color, -1, -1, 0);

        // Восстанавливаем цепочку лучших ходов.
        // Двигаемся по ссылкам next_best_state, пока не дойдём до листа
        // (next_best_state == -1) или не встретим пустой ход (x == -1).
        vector<move_pos> result;
        int current_state = 0;
        while (current_state != -1 && next_move[current_state].x != -1)
        {
            result.push_back(next_move[current_state]);
            current_state = next_best_state[current_state];
        }
        return result;
    }

    double find_first_best_turn(vector<vector<POS_T>> mtx, const bool color,
        const POS_T x, const POS_T y, size_t state,
        double alpha = -1)
    {
        // Резервируем слоты в массивах для текущего состояния.
        // -1 в next_best_state — заглушка (ссылка пока не установлена).
        // Пустой ход (-1,-1,-1,-1) — заглушка до нахождения лучшего хода.
        next_best_state.push_back(-1);
        next_move.emplace_back(-1, -1, -1, -1);

        double best_score = -1;  // Лучший результат среди всех рассмотренных ходов.

        // Если state != 0 — мы внутри серии взятий.
        // Ищем ходы только для конкретной фигуры (x, y), которая должна продолжать бить.
        if (state != 0)
            find_turns(x, y, mtx);

        auto available_turns = turns;   // Сохраняем найденные ходы (find_turns меняет поле turns).
        bool can_beat = have_beats;      // Сохраняем флаг наличия взятий.

        // Если мы были в середине серии взятий, но продолжать бить нельзя —
        // передаём ход противнику через обычный минимакс-поиск.
        if (!can_beat && state != 0)
        {
            return find_best_turns_rec(mtx, 1 - color, 0, alpha);
        }

        // Перебираем все возможные ходы из текущей позиции.
        for (auto& turn : available_turns)
        {
            // Индекс следующего состояния (используется при серии взятий
            // для связывания состояний в цепочку).
            size_t next_state = next_move.size();
            double score;

            if (can_beat)
            {
                // Взятие: та же фигура продолжает серию.
                // Применяем ход к копии доски и рекурсивно ищем продолжение
                // с обновлёнными координатами фигуры (turn.x2, turn.y2).
                // Передаём best_score как alpha для отсечения худших веток.
                score = find_first_best_turn(
                    make_turn(mtx, turn), color, turn.x2, turn.y2,
                    next_state, best_score);
            }
            else
            {
                // Обычный ход: ход бота завершён, передаём ход противнику.
                // Запускаем минимакс-поиск для цвета противника с глубины 0.
                // best_score используется как alpha (нижняя граница для отсечения).
                score = find_best_turns_rec(
                    make_turn(mtx, turn), 1 - color, 0, best_score);
            }

            // Если найденный результат лучше текущего лучшего — обновляем.
            if (score > best_score)
            {
                best_score = score;
                // Для взятия: указываем ссылку на следующее состояние (продолжение серии).
                // Для обычного хода: -1 (противник будет ходить в отдельном поддереве).
                next_best_state[state] = (can_beat ? int(next_state) : -1);
                next_move[state] = turn;
            }
        }
        return best_score;
    }

    double find_best_turns_rec(vector<vector<POS_T>> mtx, const bool color,
        const size_t depth, double alpha = -1,
        double beta = INF + 1,
        const POS_T x = -1, const POS_T y = -1)
    {
        // Базовый случай: достигнута максимальная глубина поиска.
        // Оцениваем позицию эвристической функцией calc_score.
        // Параметр (depth % 2 == color) корректирует перспективу:
        // результат всегда возвращается с точки зрения бота.
        if (depth == Max_depth)
        {
            return calc_score(mtx, (depth % 2 == color));
        }

        // Если заданы координаты (x != -1) — мы в середине серии взятий.
        // Ищем ходы только для указанной фигуры.
        if (x != -1)
        {
            find_turns(x, y, mtx);
        }
        else
        {
            // Иначе ищем все ходы для текущего цвета.
            find_turns(color, mtx);
        }

        auto available_turns = turns;
        bool can_beat = have_beats;

        // Если были в середине серии взятий, но продолжать бить нельзя —
        // передаём ход противнику, увеличивая глубину на 1.
        if (!can_beat && x != -1)
        {
            return find_best_turns_rec(mtx, 1 - color, depth + 1, alpha, beta);
        }

        // Нет ходов вообще — текущий игрок проигрывает.
        // depth % 2 == 0 (чётная, ход противника): противник не может ходить
        //   → бот выиграл → возвращаем INF (максимально хорошая позиция).
        // depth % 2 == 1 (нечётная, ход бота): бот не может ходить
        //   → бот проиграл → возвращаем 0 (максимально плохая позиция).
        if (available_turns.empty())
        {
            return (depth % 2 ? 0 : INF);
        }

        // Инициализация для минимакса.
        // На чётных глубинах (ход противника) — минимизация (ищем минимум).
        // На нечётных глубинах (ход бота) — максимизация (ищем максимум).
        double best_min = INF + 1;   // Лучший результат для минимизирующего игрока.
        double best_max = -1;        // Лучший результат для максимизирующего игрока.

        for (auto& turn : available_turns)
        {
            double score;

            if (!can_beat && x == -1)
            {
                // Обычный ход (не взятие, не продолжение серии):
                // применяем ход и передаём ход противнику.
                // Глубина увеличивается на 1.
                score = find_best_turns_rec(
                    make_turn(mtx, turn), 1 - color, depth + 1, alpha, beta);
            }
            else
            {
                // Взятие (или продолжение серии взятий):
                // тот же игрок ходит снова, глубина не меняется.
                // Передаём координаты фигуры после хода для проверки продолжения серии.
                score = find_best_turns_rec(
                    make_turn(mtx, turn), color, depth, alpha, beta,
                    turn.x2, turn.y2);
            }

            // Обновляем лучшие результаты для обоих типов уровней.
            best_min = min(best_min, score);
            best_max = max(best_max, score);

            // --- Альфа-бета отсечение ---
            // На нечётной глуботе (ход бота, максимизация):
            //   alpha — нижняя граница: бот гарантированно получит не меньше.
            //   Обновляем alpha, если нашли ход лучше.
            // На чётной глуботе (ход противника, минимизация):
            //   beta — верхняя граница: противник гарантированно даст не больше.
            //   Обновляем beta, если нашли для противника ход хуже (лучше для бота).
            if (depth % 2)
            {
                alpha = max(alpha, best_max);
            }
            else
            {
                beta = min(beta, best_min);
            }

            // Если оптимизация включена и границы пересеклись (alpha >= beta) —
            // дальнейший перебор не имеет смысла: отсекаем ветку.
            // Возвращаем значение с поправкой (+1 или -1), чтобы отсечённый
            // результат не был случайно выбран как реальный лучший на верхнем уровне.
            if (optimization != "O0" && alpha >= beta)
            {
                return (depth % 2 ? best_max + 1 : best_min - 1);
            }
        }

        // Возвращаем результат в зависимости от типа текущего уровня:
        // нечётная глубина (бот) — максимум, чётная (противник) — минимум.
        return (depth % 2 ? best_max : best_min);
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
