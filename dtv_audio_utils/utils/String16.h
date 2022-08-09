/*
 * Copyright (C) 2005 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef ANDROID_STRING16_H
#define ANDROID_STRING16_H

#include <string> // for std::string

#include <DrmpErrors.h>
#include <String8.h>
#include <TypeHelpers.h>

// ---------------------------------------------------------------------------

extern "C" {

}

class String8;

// DO NOT USE: please use std::u16string

//! This is a string holding UTF-16 characters.
class String16
{
public:
    /* use String16(StaticLinkage) if you're statically linking against
     * libutils and declaring an empty static String16, e.g.:
     *
     *   static String16 sAStaticEmptyString(String16::kEmptyString);
     *   static String16 sAnotherStaticEmptyString(sAStaticEmptyString);
     */
    enum StaticLinkage { kEmptyString };

                                String16();
    explicit                    String16(StaticLinkage);
                                String16(const String16& o);
                                String16(const String16& o,
                                         size_t len,
                                         size_t begin=0);
    explicit                    String16(const char16_t* o);
    explicit                    String16(const char16_t* o, size_t len);
    explicit                    String16(const String8& o);
    explicit                    String16(const char* o);
    explicit                    String16(const char* o, size_t len);

                                ~String16();

    inline  const char16_t*     string() const;

private:
    static inline std::string   std_string(const String16& str);
public:
            size_t              size() const;
            void                setTo(const String16& other);
            dp_state            setTo(const char16_t* other);
            dp_state            setTo(const char16_t* other, size_t len);
            dp_state            setTo(const String16& other,
                                      size_t len,
                                      size_t begin=0);

            dp_state            append(const String16& other);
            dp_state            append(const char16_t* other, size_t len);

    inline  String16&           operator=(const String16& other);

    inline  String16&           operator+=(const String16& other);
    inline  String16            operator+(const String16& other) const;

            dp_state            insert(size_t pos, const char16_t* chrs);
            dp_state            insert(size_t pos,
                                       const char16_t* chrs, size_t len);

            ssize_t             findFirst(char16_t c) const;
            ssize_t             findLast(char16_t c) const;

            bool                startsWith(const String16& prefix) const;
            bool                startsWith(const char16_t* prefix) const;

            bool                contains(const char16_t* chrs) const;

            dp_state            makeLower();

            dp_state            replaceAll(char16_t replaceThis,
                                           char16_t withThis);

            dp_state            remove(size_t len, size_t begin=0);

    inline  int                 compare(const String16& other) const;

    inline  bool                operator<(const String16& other) const;
    inline  bool                operator<=(const String16& other) const;
    inline  bool                operator==(const String16& other) const;
    inline  bool                operator!=(const String16& other) const;
    inline  bool                operator>=(const String16& other) const;
    inline  bool                operator>(const String16& other) const;

    inline  bool                operator<(const char16_t* other) const;
    inline  bool                operator<=(const char16_t* other) const;
    inline  bool                operator==(const char16_t* other) const;
    inline  bool                operator!=(const char16_t* other) const;
    inline  bool                operator>=(const char16_t* other) const;
    inline  bool                operator>(const char16_t* other) const;

    inline                      operator const char16_t*() const;

private:
            const char16_t*     mString;
};

// String16 can be trivially moved using memcpy() because moving does not
// require any change to the underlying SharedBuffer contents or reference count.
ANDROID_TRIVIAL_MOVE_TRAIT(String16)

// ---------------------------------------------------------------------------
// No user serviceable parts below.

inline int compare_type(const String16& lhs, const String16& rhs)
{
    return lhs.compare(rhs);
}

inline int strictly_order_type(const String16& lhs, const String16& rhs)
{
    return compare_type(lhs, rhs) < 0;
}

inline const char16_t* String16::string() const
{
    return mString;
}

inline std::string String16::std_string(const String16& str)
{
    return std::string(String8(str).string());
}

inline String16& String16::operator=(const String16& other)
{
    setTo(other);
    return *this;
}

inline String16& String16::operator+=(const String16& other)
{
    append(other);
    return *this;
}

inline String16 String16::operator+(const String16& other) const
{
    String16 tmp(*this);
    tmp += other;
    return tmp;
}

inline int String16::compare(const String16& other) const
{
    return strzcmp16(mString, size(), other.mString, other.size());
}

inline bool String16::operator<(const String16& other) const
{
    return strzcmp16(mString, size(), other.mString, other.size()) < 0;
}

inline bool String16::operator<=(const String16& other) const
{
    return strzcmp16(mString, size(), other.mString, other.size()) <= 0;
}

inline bool String16::operator==(const String16& other) const
{
    return strzcmp16(mString, size(), other.mString, other.size()) == 0;
}

inline bool String16::operator!=(const String16& other) const
{
    return strzcmp16(mString, size(), other.mString, other.size()) != 0;
}

inline bool String16::operator>=(const String16& other) const
{
    return strzcmp16(mString, size(), other.mString, other.size()) >= 0;
}

inline bool String16::operator>(const String16& other) const
{
    return strzcmp16(mString, size(), other.mString, other.size()) > 0;
}

inline bool String16::operator<(const char16_t* other) const
{
    return strcmp16(mString, other) < 0;
}

inline bool String16::operator<=(const char16_t* other) const
{
    return strcmp16(mString, other) <= 0;
}

inline bool String16::operator==(const char16_t* other) const
{
    return strcmp16(mString, other) == 0;
}

inline bool String16::operator!=(const char16_t* other) const
{
    return strcmp16(mString, other) != 0;
}

inline bool String16::operator>=(const char16_t* other) const
{
    return strcmp16(mString, other) >= 0;
}

inline bool String16::operator>(const char16_t* other) const
{
    return strcmp16(mString, other) > 0;
}

inline String16::operator const char16_t*() const
{
    return mString;
}

// ---------------------------------------------------------------------------

#endif // ANDROID_STRING16_H
