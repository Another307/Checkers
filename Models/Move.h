#pragma once
#include <stdlib.h>

typedef int8_t POS_T;

struct move_pos
{
    POS_T x, y;             // Координаты исходной клетки
    POS_T x2, y2;           // Координаты целевой клетки
    POS_T xb = -1, yb = -1; // Координаты побитой фигуры

    move_pos(const POS_T x, const POS_T y, const POS_T x2, const POS_T y2) : x(x), y(y), x2(x2), y2(y2)   // Конструктор обычного хода
    {
    }
    move_pos(const POS_T x, const POS_T y, const POS_T x2, const POS_T y2, const POS_T xb, const POS_T yb)   // Конструктор хода со взятием
        : x(x), y(y), x2(x2), y2(y2), xb(xb), yb(yb)
    {
    }

    bool operator==(const move_pos &other) const     // Сравнение по «геометрии» хода для проверки, совпадает ли выбор игрока с одним из разрешённых ходов.
    {
        return (x == other.x && y == other.y && x2 == other.x2 && y2 == other.y2);
    }
    bool operator!=(const move_pos &other) const     // Если текущий ход не совпал с возможными, возвращает true
    {
        return !(*this == other);
    }
};
