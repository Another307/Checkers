#pragma once
#include <chrono>
#include <thread>

#include "../Models/Project_path.h"
#include "Board.h"
#include "Config.h"
#include "Hand.h"
#include "Logic.h"

class Game
{
  public:
    Game() : board(config("WindowSize", "Width"), config("WindowSize", "Hight")), hand(&board), logic(&board, &config)
    {
        ofstream fout(project_path + "log.txt", ios_base::trunc);
        fout.close();
    }

    // to start checkers
    int play()
    {
        auto start = chrono::steady_clock::now();  // фиксируем время начала партии
        if (is_replay)  // Если флаг is_replay установлен, сбрасываем состояние:
                        // заново инициализируем логику, перезагружаем конфиг, перерисовываем доску
        {
            logic = Logic(&board, &config);
            config.reload();
            board.redraw();
        }
        else
        {
            board.start_draw();  // При обычном старте просто начинаем отрисовку доски
        }
        is_replay = false;  // Сбрасываем флаг повтора после обработки

        int turn_num = -1;          // Счётчик ходов
        bool is_quit = false;       // Флаг выхода из игры по желанию игрока
        const int Max_turns = config("Game", "MaxNumTurns");    // Максимальное число ходов из настроек
        while (++turn_num < Max_turns)      // Основной цикл игры: выполняется, пока число ходов меньше лимита
        {
            beat_series = 0;     // Сброс счётчика серии взятий (для правил «бить обязательно»)
            logic.find_turns(turn_num % 2);    // Определяем возможные ходы для текущего цвета
            if (logic.turns.empty())    //если возможных ходов нет, игра закончится (сброс по невозможности хода)
                break;
            logic.Max_depth = config("Bot", string((turn_num % 2) ? "Black" : "White") + string("BotLevel"));     // Устанавливаем глубину поиска бота в зависимости от уровня сложности для текущего цвета
            if (!config("Bot", string("Is") + string((turn_num % 2) ? "Black" : "White") + string("Bot")))     // Проверяем, является ли текущий игрок ботом
            {
                auto resp = player_turn(turn_num % 2);     // Ход игрока: обрабатываем ввод
                if (resp == Response::QUIT)
                {
                    is_quit = true;
                    break;
                }
                else if (resp == Response::REPLAY)
                {
                    is_replay = true;     // Запрос на повтор всей партии
                    break;
                }
                else if (resp == Response::BACK)
                {
                    if (config("Bot", string("Is") + string((1 - turn_num % 2) ? "Black" : "White") + string("Bot")) &&    
                        // Откат хода (undo): проверяем условия (нет серии взятий, есть история ходов),
                       // и если возможно — отменяем ход и уменьшаем счётчик ходов.
                        !beat_series && board.history_mtx.size() > 2)
                    {
                        board.rollback();
                        --turn_num;
                    }
                    if (!beat_series)     // Дополнительная логика отката
                        --turn_num;

                    board.rollback();
                    --turn_num;
                    beat_series = 0;
                }
            }
            else
                bot_turn(turn_num % 2);   // Если текущий игрок — бот, вызываем логику хода бота
        }
        auto end = chrono::steady_clock::now();     // Фиксируем время окончания партии
        ofstream fout(project_path + "log.txt", ios_base::app);
        fout << "Game time: " << (int)chrono::duration<double, milli>(end - start).count() << " millisec\n";
        fout.close();

        if (is_replay)       // Если запрошен повтор — запускаем игру заново
            return play();
        if (is_quit)         // Если игрок вышел - завершаем игру
            return 0;
        int res = 2;
        if (turn_num == Max_turns)    // Лимит ходов исчерпан — фиксируем ничью
        {
            res = 0; 
        }
        else if (turn_num % 2)      // Последний сделанный ход был нечётным — победа чёрных
        {
            res = 1;
        }
        board.show_final(res);     // Отображаем финальный результат на доске
        auto resp = hand.wait();   // Ожидаем действия пользователя после окончания партии
        if (resp == Response::REPLAY)
        {
            is_replay = true;
            return play();    // Повтор партии
        }
        return res;
    }

  private:
    void bot_turn(const bool color)
    {
        auto start = chrono::steady_clock::now();  // Замер времени начала хода бота

        auto delay_ms = config("Bot", "BotDelayMS");  // Получаем задержку между ходами из конфига
        // new thread for equal delay for each turn
        thread th(SDL_Delay, delay_ms);     // Создаём отдельный поток для задержки, чтобы она была одинаковой для каждого хода
        auto turns = logic.find_best_turns(color);    // Находим лучшие ходы для бота
        th.join();     // Ждём завершения задержки
        bool is_first = true;     // Флаг для корректной обработки задержки между ходами в серии взятий
        // making moves
        for (auto turn : turns)   // Выполняем найденные ходы
        {
            if (!is_first)     // Между ходами в серии делаем задержку, но не перед самым первым ходом
            {
                SDL_Delay(delay_ms);
            }
            is_first = false;
            beat_series += (turn.xb != -1);    // Увеличиваем счётчик серии взятий, если текущий ход — взятие
            board.move_piece(turn, beat_series);     // Перемещаем фигуру на доске с учётом серии взятий
        }

        auto end = chrono::steady_clock::now();    // Фиксируем время окончания хода бота
        ofstream fout(project_path + "log.txt", ios_base::app);
        fout << "Bot turn time: " << (int)chrono::duration<double, milli>(end - start).count() << " millisec\n";
        fout.close();
    }

    Response player_turn(const bool color)
    {
        // return 1 if quit
        vector<pair<POS_T, POS_T>> cells;     // Собираем список клеток, с которых возможны ходы, для подсветки
        for (auto turn : logic.turns)
        {
            cells.emplace_back(turn.x, turn.y);
        }
        board.highlight_cells(cells);
        move_pos pos = {-1, -1, -1, -1};     // Позиция выбранного хода
        POS_T x = -1, y = -1;                // Координаты выбранной клетки
        // trying to make first move
        while (true)
        {
            auto resp = hand.get_cell();        // Получаем действие пользователя
            if (get<0>(resp) != Response::CELL)  // Если это не выбор клетки, возвращаем статус (вроде перезапуска или выхода)
                return get<0>(resp);
            pair<POS_T, POS_T> cell{get<1>(resp), get<2>(resp)};

            bool is_correct = false;     // Флаг корректности выбора
            for (auto turn : logic.turns)
            {
                if (turn.x == cell.first && turn.y == cell.second)      // Проверяем, совпадает ли выбранная клетка с началом одного из возможных ходов
                {
                    is_correct = true;     
                    break;
                }
                if (turn == move_pos{x, y, cell.first, cell.second})     // Альтернативное условие: если уже выбрана конечная точка, проверяем соответствие ходу
                {
                    pos = turn;
                    break;
                }
            }
            if (pos.x != -1)     // Если ход уже определён, выходим из цикла выбора
                break;
            if (!is_correct)     // Если выбор некорректен: сбрасываем выделение и подсвечиваем исходные клетки заново
            {
                if (x != -1)
                {
                    board.clear_active();
                    board.clear_highlight();
                    board.highlight_cells(cells);
                }
                x = -1;     
                y = -1;
                continue;
            }
            x = cell.first;     // Запоминаем выбранную клетку как начальную точку хода
            y = cell.second;
            board.clear_highlight();
            board.set_active(x, y);
            vector<pair<POS_T, POS_T>> cells2;   // Подсвечиваем возможные конечные точки для выбранной клетки
            for (auto turn : logic.turns)
            {
                if (turn.x == x && turn.y == y)
                {
                    cells2.emplace_back(turn.x2, turn.y2);
                }
            }
            board.highlight_cells(cells2);
        }
        board.clear_highlight();     // Убираем подсветку и выполняем ход на доске
        board.clear_active();
        board.move_piece(pos, pos.xb != -1);
        if (pos.xb == -1)     // Если это был обычный ход (без взятия), завершаем ход игрока
            return Response::OK;
        // continue beating while can
        beat_series = 1;     // Иначе — начинаем обработку серии взятий: игрок обязан продолжать бить, пока возможно
        while (true)
        {
            logic.find_turns(pos.x2, pos.y2);    // Ищем возможные взятия из текущей позиции
            if (!logic.have_beats)     // Если бить больше нельзя — завершаем серию взятий
                break;     

            vector<pair<POS_T, POS_T>> cells;     // Подсвечиваем клетки, куда можно побить
            for (auto turn : logic.turns)
            {
                cells.emplace_back(turn.x2, turn.y2);
            }
            board.highlight_cells(cells);
            board.set_active(pos.x2, pos.y2);
            // trying to make move
            while (true)    // Цикл выбора следующего хода в серии взятий
            {
                auto resp = hand.get_cell();
                if (get<0>(resp) != Response::CELL)
                    return get<0>(resp);
                pair<POS_T, POS_T> cell{get<1>(resp), get<2>(resp)};

                bool is_correct = false;
                for (auto turn : logic.turns)
                {
                    if (turn.x2 == cell.first && turn.y2 == cell.second)   // Проверяем, соответствует ли выбор одному из возможных взятий
                    {
                        is_correct = true;
                        pos = turn;
                        break;
                    }
                }
                if (!is_correct)  
                    continue;    // Некорректный выбор — ждём следующего клика

                board.clear_highlight();
                board.clear_active();
                beat_series += 1;     // Увеличиваем счётчик взятий
                board.move_piece(pos, beat_series);   // Выполняем взятие
                break;       // Переходим к следующей итерации проверки возможности бить дальше
            } 
        }

        return Response::OK;     // Ход игрока завершён успешно
    }

  private:
    Config config;
    Board board;
    Hand hand;
    Logic logic;
    int beat_series;
    bool is_replay = false;
};
