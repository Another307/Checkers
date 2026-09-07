#pragma once
#include <tuple>

#include "../Models/Move.h"
#include "../Models/Response.h"
#include "Board.h"

// methods for hands
class Hand
{
  public:
    Hand(Board *board) : board(board)
    {
    }
    tuple<Response, POS_T, POS_T> get_cell() const
    {
        SDL_Event windowEvent;
        Response resp = Response::OK;
        int x = -1, y = -1;     // Координаты клика (в пикселях)
        int xc = -1, yc = -1;     // Координаты клетки на доске
        while (true)     // Бесконечный цикл опроса событий — прерывается, когда получен значимый ответ
        {
            if (SDL_PollEvent(&windowEvent))
            {
                switch (windowEvent.type)
                {
                case SDL_QUIT:
                    resp = Response::QUIT;     // Пользователь закрыл окно крестиком
                    break;
                case SDL_MOUSEBUTTONDOWN:     // Клик мыши: получаем экранные координаты
                    x = windowEvent.motion.x;     // Номер строки
                    y = windowEvent.motion.y;     // Номер столбца
                    xc = int(y / (board->H / 10) - 1);
                    yc = int(x / (board->W / 10) - 1);
                    if (xc == -1 && yc == -1 && board->history_mtx.size() > 1)     // зона -  Откат хода (при условии наличия истории откатов)
                    {
                        resp = Response::BACK;
                    }
                    else if (xc == -1 && yc == 8)     // зона -  Повтор партии
                    {
                        resp = Response::REPLAY;
                    }
                    else if (xc >= 0 && xc < 8 && yc >= 0 && yc < 8)     // зона - выбор клетки 
                    {
                        resp = Response::CELL;
                    }
                    else     // зона вне поля и кнопок, отмена выбора клетки/игнор
                    {
                        xc = -1;
                        yc = -1;
                    }
                    break;
                case SDL_WINDOWEVENT:     
                    if (windowEvent.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)     // Изменение размера окна
                    {
                        board->reset_window_size();
                        break;
                    }
                }
                if (resp != Response::OK)     // Если получили значимый ответ (не OK) — выходим из цикла опроса
                    break;
            }
        }
        return {resp, xc, yc};     // Возвращаем статус и координаты клетки
    }

    Response wait() const
    {
        SDL_Event windowEvent;
        Response resp = Response::OK;
        while (true)     // Аналогичный цикл опроса событий, но упрощённый
        {
            if (SDL_PollEvent(&windowEvent))     
            {
                switch (windowEvent.type)
                {
                case SDL_QUIT:
                    resp = Response::QUIT;     // Отклик на кнопку выхода
                    break;
                case SDL_WINDOWEVENT_SIZE_CHANGED:     // Изменение размера окна
                    board->reset_window_size();
                    break;
                case SDL_MOUSEBUTTONDOWN: {
                    int x = windowEvent.motion.x;     // Строка
                    int y = windowEvent.motion.y;     // Столбец
                    int xc = int(y / (board->H / 10) - 1);
                    int yc = int(x / (board->W / 10) - 1);
                    if (xc == -1 && yc == 8)
                        resp = Response::REPLAY;
                }
                break;
                }
                if (resp != Response::OK)
                    break;
            }
        }
        return resp;
    }

  private:
    Board *board;
};
