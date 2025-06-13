#ifndef GUARD_FIELD_SEASONS_H
#define GUARD_FIELD_SEASONS_H

enum {
    SEASON_SPRING,
    SEASON_SUMMER,
    SEASON_AUTUMN,
    SEASON_WINTER,
    SEASONS_COUNT,
};

const struct Tileset* GetMapLayoutPrimaryTileset(struct MapLayout const *mapLayout);

#endif //GUARD_FIELD_SEASONS_H