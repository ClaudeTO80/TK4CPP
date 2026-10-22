# Third-party notices

TK4CPP itself is MIT licensed (see `LICENSE`). It bundles or uses the following third-party software.

## jwt-cpp 0.7.1 (vendored in `3rdParty/jwt-cpp`)

- Project: https://github.com/Thalhammer/jwt-cpp
- License: MIT, Copyright (c) 2018 Dominik Thalhammer (full text in `3rdParty/jwt-cpp/LICENSE`)
- Used by: `JWT` module (header-only, implementation detail)

## picojson (bundled inside jwt-cpp)

- Project: https://github.com/kazuho/picojson
- License: BSD-2-Clause, Copyright 2009-2010 Cybozu Labs, Inc. / Copyright 2011-2014 Kazuho Oku (notice at the top of `3rdParty/jwt-cpp/include/picojson/picojson.h`)
- Used by: `JWT` module, through jwt-cpp

## libxml2 2.16.0 (vendored in `3rdParty/libxml2`, used when no suitable system copy is found)

- Project: https://gitlab.gnome.org/GNOME/libxml2
- License: MIT, Copyright (C) 1998-2012 Daniel Veillard and contributors (full text in `3rdParty/libxml2/Copyright`)
- Used by: `XML` module (implementation detail, no libxml2 header is part of the public API)

## GoogleTest 1.15.2 (tests only, downloaded at configure time, not distributed)

- Project: https://github.com/google/googletest
- License: BSD-3-Clause
