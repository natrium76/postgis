-- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
-- 
-- PostGIS - Spatial Types for PostgreSQL
-- http://postgis.refractions.net
--
-- Copyright (C) 2011-2012 Sandro Santilli <strk@keybit.net>
--
-- This is free software; you can redistribute and/or modify it under
-- the terms of the GNU General Public Licence. See the COPYING file.
--
-- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

-- {
--  Convert a simple geometry to a topologically-defined one
--
--  See http://trac.osgeo.org/postgis/ticket/1017
--
-- }{
CREATE OR REPLACE FUNCTION topology.toTopoGeom(ageom Geometry, atopology varchar, alayer int, atolerance float8 DEFAULT 0)
  RETURNS topology.TopoGeometry
AS
$$
DECLARE
  layer_info RECORD;
  topology_info RECORD;
  rec RECORD;
  tg topology.TopoGeometry;
  elems topology.TopoElementArray = '{{0,0}}';
  sql TEXT;
  typ TEXT;
BEGIN

  -- Get topology information
  BEGIN
    SELECT *
    FROM topology.topology
      INTO STRICT topology_info WHERE name = atopology;
  EXCEPTION
    WHEN NO_DATA_FOUND THEN
      RAISE EXCEPTION 'No topology with name "%" in topology.topology',
        atopology;
  END;

  -- Get layer information
  BEGIN
    SELECT *, CASE
      WHEN feature_type = 1 THEN 'puntal'
      WHEN feature_type = 2 THEN 'lineal'
      WHEN feature_type = 3 THEN 'areal'
      WHEN feature_type = 4 THEN 'mixed'
      ELSE 'unexpected_'||feature_type
      END as typename
    FROM topology.layer l
      INTO STRICT layer_info
      WHERE l.layer_id = alayer
      AND l.topology_id = topology_info.id;
  EXCEPTION
    WHEN NO_DATA_FOUND THEN
      RAISE EXCEPTION 'No layer with id "%" in topology "%"',
        alayer, atopology;
  END;

  -- Can't convert to a hierarchical topogeometry
  IF layer_info.level > 0 THEN
      RAISE EXCEPTION 'Layer "%" of topology "%" is hierarchical, cannot convert to it.',
        alayer, atopology;
  END IF;


  -- 
  -- Check type compatibility and create empty TopoGeometry
  -- 1:puntal, 2:lineal, 3:areal, 4:collection
  --
  typ = geometrytype(ageom);
  IF typ = 'GEOMETRYCOLLECTION' THEN
    --  A collection can only go collection layer
    IF layer_info.feature_type != 4 THEN
      RAISE EXCEPTION
        'Layer "%" of topology "%" is %, cannot hold a collection feature.',
        layer_info.layer_id, topology_info.name, layer_info.typename;
    END IF;
    tg := topology.CreateTopoGeom(atopology, 4, alayer);
  ELSIF typ = 'POINT' OR typ = 'MULTIPOINT' THEN -- puntal
    --  A point can go in puntal or collection layer
    IF layer_info.feature_type != 4 and layer_info.feature_type != 1 THEN
      RAISE EXCEPTION
        'Layer "%" of topology "%" is %, cannot hold a puntal feature.',
        layer_info.layer_id, topology_info.name, layer_info.typename;
    END IF;
    tg := topology.CreateTopoGeom(atopology, 1, alayer);
  ELSIF typ = 'LINESTRING' or typ = 'MULTILINESTRING' THEN -- lineal
    --  A line can go in lineal or collection layer
    IF layer_info.feature_type != 4 and layer_info.feature_type != 2 THEN
      RAISE EXCEPTION
        'Layer "%" of topology "%" is %, cannot hold a lineal feature.',
        layer_info.layer_id, topology_info.name, layer_info.typename;
    END IF;
    tg := topology.CreateTopoGeom(atopology, 2, alayer);
  ELSIF typ = 'POLYGON' OR typ = 'MULTIPOLYGON' THEN -- areal
    --  An area can go in areal or collection layer
    IF layer_info.feature_type != 4 and layer_info.feature_type != 3 THEN
      RAISE EXCEPTION
        'Layer "%" of topology "%" is %, cannot hold an areal feature.',
        layer_info.layer_id, topology_info.name, layer_info.typename;
    END IF;
    tg := topology.CreateTopoGeom(atopology, 3, alayer);
  ELSE
      -- Should never happen
      RAISE EXCEPTION
        'Unsupported feature type %', typ;
  END IF;

  tg := topology.toTopoGeom(ageom, tg, atolerance);

  RETURN tg;

END
$$
LANGUAGE 'plpgsql' VOLATILE STRICT;
-- }

-- {
--  Convert a simple geometry to a topologically-defined one
--  adding its components to a pre-existing TopoGeometry
--
-- }{
CREATE OR REPLACE FUNCTION topology.toTopoGeom(ageom Geometry, tg topology.TopoGeometry, atolerance float8 DEFAULT 0)
  RETURNS topology.TopoGeometry
AS
$$
DECLARE
  layer_info RECORD;
  topology_info RECORD;
  rec RECORD;
  elems topology.TopoElementArray = '{{0,0}}';
  sql TEXT;
  typ TEXT;
  tolerance FLOAT8;
  alayer INT;
  atopology TEXT;
BEGIN

  RAISE NOTICE 'TopoGeometry is "%", its topology_id is "%"', tg, topology_id(tg);

  -- Get topology information
  SELECT id, name FROM topology.topology
    INTO topology_info
    WHERE id = topology_id(tg);
  IF NOT FOUND THEN
    RAISE EXCEPTION 'No topology with id "%" in topology.topology',
                    topology_id(tg);
  END IF;

  alayer := layer_id(tg);
  atopology := topology_info.name;


  -- Get tolerance, if 0 was given
  tolerance := COALESCE( NULLIF(atolerance, 0), topology._st_mintolerance(topology_info.name, ageom) );

  -- Get layer information
  BEGIN
    SELECT *, CASE
      WHEN feature_type = 1 THEN 'puntal'
      WHEN feature_type = 2 THEN 'lineal'
      WHEN feature_type = 3 THEN 'areal'
      WHEN feature_type = 4 THEN 'mixed'
      ELSE 'unexpected_'||feature_type
      END as typename
    FROM topology.layer l
      INTO STRICT layer_info
      WHERE l.layer_id = layer_id(tg)
      AND l.topology_id = topology_info.id;
  EXCEPTION
    WHEN NO_DATA_FOUND THEN
      RAISE EXCEPTION 'No layer with id "%" in topology "%"',
        alayer, atopology;
  END;

  -- Can't convert to a hierarchical topogeometry
  IF layer_info.level > 0 THEN
      RAISE EXCEPTION 'Layer "%" of topology "%" is hierarchical, cannot convert a simple geometry to it.',
        alayer, atopology;
  END IF;


  -- 
  -- Check type compatibility and set TopoGeometry type
  -- 1:puntal, 2:lineal, 3:areal, 4:collection
  --
  typ = geometrytype(ageom);
  IF typ = 'GEOMETRYCOLLECTION' THEN
    --  A collection can only go to collection layer
    IF layer_info.feature_type != 4 THEN
      RAISE EXCEPTION
        'Layer "%" of topology "%" is %, cannot hold a collection feature.',
        layer_info.layer_id, topology_info.name, layer_info.typename;
    END IF;
    tg.type := 4;
  ELSIF typ = 'POINT' OR typ = 'MULTIPOINT' THEN -- puntal
    --  A point can go in puntal or collection layer
    IF layer_info.feature_type != 4 and layer_info.feature_type != 1 THEN
      RAISE EXCEPTION
        'Layer "%" of topology "%" is %, cannot hold a puntal feature.',
        layer_info.layer_id, topology_info.name, layer_info.typename;
    END IF;
    tg.type := 1;
  ELSIF typ = 'LINESTRING' or typ = 'MULTILINESTRING' THEN -- lineal
    --  A line can go in lineal or collection layer
    IF layer_info.feature_type != 4 and layer_info.feature_type != 2 THEN
      RAISE EXCEPTION
        'Layer "%" of topology "%" is %, cannot hold a lineal feature.',
        layer_info.layer_id, topology_info.name, layer_info.typename;
    END IF;
    tg.type := 2;
  ELSIF typ = 'POLYGON' OR typ = 'MULTIPOLYGON' THEN -- areal
    --  An area can go in areal or collection layer
    IF layer_info.feature_type != 4 and layer_info.feature_type != 3 THEN
      RAISE EXCEPTION
        'Layer "%" of topology "%" is %, cannot hold an areal feature.',
        layer_info.layer_id, topology_info.name, layer_info.typename;
    END IF;
    tg.type := 3;
  ELSE
      -- Should never happen
      RAISE EXCEPTION
        'Unexpected feature dimension %', ST_Dimension(ageom);
  END IF;

  -- Now that we have an empty topogeometry, we loop over distinct components 
  -- and add them to the definition of it. We add them as soon
  -- as possible so that each element can further edit the
  -- definition by splitting
  FOR rec IN SELECT DISTINCT id(tg), alayer as lyr,
    CASE WHEN ST_Dimension(geom) = 0 THEN 1
         WHEN ST_Dimension(geom) = 1 THEN 2
         WHEN ST_Dimension(geom) = 2 THEN 3
    END as type,
    CASE WHEN ST_Dimension(geom) = 0 THEN
           topology.topogeo_addPoint(topology_info.name, geom, tolerance)
         WHEN ST_Dimension(geom) = 1 THEN
           topology.topogeo_addLineString(topology_info.name, geom, tolerance)
         WHEN ST_Dimension(geom) = 2 THEN
           topology.topogeo_addPolygon(topology_info.name, geom, tolerance)
    END as primitive
    FROM (SELECT (ST_Dump(ageom)).geom) as f
    WHERE NOT ST_IsEmpty(geom)
  LOOP
    -- TODO: consider use a single INSERT statement for the whole thing
    sql := 'INSERT INTO ' || quote_ident(topology_info.name)
        || '.relation(topogeo_id, layer_id, element_type, element_id) SELECT '
        || rec.id || ',' || rec.lyr || ',' || rec.type
        || ',' || rec.primitive
        -- NOTE: we're avoiding duplicated rows here
        || ' EXCEPT SELECT ' || rec.id || ', ' || rec.lyr
        || ', element_type, element_id FROM '
        || quote_ident(topology_info.name)
        || '.relation WHERE layer_id = ' || rec.lyr
        || ' AND topogeo_id = ' || rec.id; 
#ifdef POSTGIS_TOPOLOGY_DEBUG
    RAISE DEBUG '%', sql;
#endif
    EXECUTE sql;
  END LOOP;

  RETURN tg;

END
$$
LANGUAGE 'plpgsql' VOLATILE STRICT;
-- }
