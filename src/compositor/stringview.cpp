/*
 * Copyright 2015 Giulio Camuffo <giuliocamuffo@gmail.com>
 *
 * This file is part of Orbital
 *
 * Orbital is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Orbital is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Orbital.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "stringview.h"

namespace Orbital {

StringView::StringView()
          : string(nullptr)
          , end(nullptr)
{
}

StringView::StringView(const char *str, size_t l)
          : string(str)
          , end(str + l)
{
}

StringView::StringView(const char *str)
          : StringView(str, strlen(str))
{
}

StringView::StringView(const std::string &str)
          : StringView(str.data(), str.size())
{
}

StringView::StringView(const QByteArray &str)
          : StringView(str.constData(), str.size())
{
}

std::string StringView::toStdString() const
{
    return std::string(string, size());
}

bool StringView::operator==(StringView v) const
{
    return size() == v.size() && memcmp(string, v.string, size()) == 0;
}

}
