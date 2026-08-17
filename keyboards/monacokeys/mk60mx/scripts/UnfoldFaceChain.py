"""
UnfoldFaceChain
----------------
Fusion 360 Script.

Purpose:
  You have bodies made of a simple LINEAR CHAIN of flat (planar) faces
  connected by straight fold edges (e.g. a curved column of keywell panels
  that you extracted as a surface). This script automatically "unfolds"
  each selected body by splitting it into one body per panel and rotating
  each panel about its fold edge until the whole chain lies flat in one
  plane -- the same rigid-body-rotation technique you'd do by hand with
  Move/Copy, just automated across as many bodies as you select at once.

Requirements / assumptions:
  - Select the CLEAN extracted surface bodies (just the panel faces),
    not a thickened solid with extra side-wall/pocket geometry. Extra
    planar faces (like keywell pocket walls) will confuse the chain
    detection and the script will raise an error naming the body.
  - Each body must be a simple chain: every face touches at most 2
    neighboring faces, and exactly 2 faces (the ends) touch only 1
    neighbor. Branching or looped shapes are not supported.
  - Fold edges must be straight lines (this is the normal case for
    faceted panels).

How to use:
  1. Utilities tab -> Add-ins panel -> Scripts and Add-ins -> Scripts tab
     -> green "+" (Add) -> browse to this file -> Open.
  2. In the Fusion browser/viewport, select all the bodies you want to
     unfold (Ctrl/Cmd-click to multi-select). You can select all 16 at
     once if they're all valid chains.
  3. With them selected, select this script in the Scripts and Add-ins
     dialog and click Run.
  4. New bodies named "<original name>_seg1", "_seg2", etc. will appear
     in the root component, laid out flat. The original bodies are left
     untouched.

If it errors on a specific body, that's useful diagnostic info -- it
means that body's face/edge topology doesn't match the simple-chain
assumption above (most often: it still contains non-chain planar faces).
"""

import adsk.core
import adsk.fusion
import traceback
import math


def run(context):
    ui = None
    try:
        app = adsk.core.Application.get()
        ui = app.userInterface
        design = adsk.fusion.Design.cast(app.activeProduct)
        if not design:
            ui.messageBox('No active Fusion design.')
            return
        rootComp = design.rootComponent

        selections = ui.activeSelections
        bodies = []
        for i in range(selections.count):
            entity = selections.item(i).entity
            if isinstance(entity, adsk.fusion.BRepBody):
                bodies.append(entity)

        if not bodies:
            ui.messageBox('Select one or more bodies (each a chain of flat '
                           'faces joined by straight fold edges) before '
                           'running this script.')
            return

        tempMgr = adsk.fusion.TemporaryBRepManager.get()

        successes = []
        failures = []
        for body in bodies:
            try:
                new_bodies = unfold_body(body, rootComp, tempMgr)
                successes.append((body.name, len(new_bodies)))
            except Exception as body_err:
                failures.append((body.name, str(body_err)))

        msg_lines = []
        if successes:
            msg_lines.append('Unfolded successfully:')
            for name, count in successes:
                msg_lines.append(f'  - {name}  ({count} segments)')
        if failures:
            msg_lines.append('')
            msg_lines.append('Failed:')
            for name, err in failures:
                msg_lines.append(f'  - {name}: {err}')

        ui.messageBox('\n'.join(msg_lines) if msg_lines else 'Nothing processed.')

    except:
        if ui:
            ui.messageBox('Script failed:\n{}'.format(traceback.format_exc()))


def edge_key(edge):
    """Order-independent identity for an edge based on its endpoint coords."""
    p1 = edge.startVertex.geometry
    p2 = edge.endVertex.geometry

    def rnd(p):
        return (round(p.x, 6), round(p.y, 6), round(p.z, 6))

    a, b = rnd(p1), rnd(p2)
    return tuple(sorted([a, b]))


def get_face_normal(face):
    point = face.pointOnFace
    _, normal = face.evaluator.getNormalAtPoint(point)
    return normal


def signed_angle_about_axis(v_from, v_to, axis):
    """Signed angle (radians) to rotate v_from onto v_to about axis
    (right-hand rule), where v_from/v_to are already roughly
    perpendicular to axis (true for planar-face normals sharing an edge
    that lies on that axis)."""
    cross = v_from.crossProduct(v_to)
    sin_part = cross.dotProduct(axis)
    cos_part = v_from.dotProduct(v_to)
    return math.atan2(sin_part, cos_part)


def unfold_body(body, rootComp, tempMgr):
    # 1. Collect planar faces only.
    planar_faces = [f for f in body.faces
                     if f.geometry.surfaceType == adsk.core.SurfaceTypes.PlaneSurfaceType]
    n = len(planar_faces)
    if n < 2:
        raise RuntimeError(f'found only {n} planar face(s); need at least 2.')

    # 2. Build adjacency: two faces are neighbors if they share an edge.
    face_edge_keys = {f: {edge_key(e) for e in f.edges} for f in planar_faces}
    adjacency = {f: [] for f in planar_faces}
    for i in range(n):
        for j in range(i + 1, n):
            fi, fj = planar_faces[i], planar_faces[j]
            shared = face_edge_keys[fi] & face_edge_keys[fj]
            if shared:
                shared_key = next(iter(shared))
                shared_edge = next(e for e in fi.edges if edge_key(e) == shared_key)
                adjacency[fi].append((fj, shared_edge))
                adjacency[fj].append((fi, shared_edge))

    # sanity check: chain, not a branch/loop
    neighbor_counts = {f: len(adjacency[f]) for f in planar_faces}
    end_faces = [f for f, c in neighbor_counts.items() if c == 1]
    if len(end_faces) != 2 or any(c > 2 for c in neighbor_counts.values()):
        raise RuntimeError('faces do not form a simple linear chain '
                            '(check for extra pocket/wall faces).')

    # 3. Walk the chain from one end to the other.
    start_face = end_faces[0]
    ordered = [start_face]
    edges_in_order = []
    visited = {start_face}
    current = start_face
    while len(ordered) < n:
        nxt = next((item for item in adjacency[current] if item[0] not in visited), None)
        if nxt is None:
            raise RuntimeError('chain walk broke before covering all faces.')
        nxt_face, shared_edge = nxt
        ordered.append(nxt_face)
        edges_in_order.append(shared_edge)
        visited.add(nxt_face)
        current = nxt_face

    # 4. Copy each face into its own standalone body (positions unchanged).
    new_bodies = []
    for idx, f in enumerate(ordered):
        tmp = tempMgr.copy(f)
        nb = rootComp.bRepBodies.add(tmp)
        nb.name = f'{body.name}_seg{idx + 1}'
        new_bodies.append(nb)

    # 5. For each fold, find the matching edge on the just-created (still
    #    unrotated) downstream body, so we can query its LIVE geometry
    #    later, after upstream rotations have moved it into place.
    live_fold_edges = []
    for k in range(n - 1):
        target_key = edge_key(edges_in_order[k])
        match = next((e for e in new_bodies[k].edges if edge_key(e) == target_key), None)
        if match is None:
            raise RuntimeError(f'could not relocate fold edge {k + 1} on the copied body.')
        live_fold_edges.append(match)

    # 6. Walk the chain again, rotating each downstream group flat.
    moveFeats = rootComp.features.moveFeatures
    for k in range(n - 1):
        edge_geom = live_fold_edges[k].geometry  # current (possibly already-moved) Line3D
        pt1 = edge_geom.startPoint
        pt2 = edge_geom.endPoint
        axis_point = pt1
        axis_dir = adsk.core.Vector3D.create(pt2.x - pt1.x, pt2.y - pt1.y, pt2.z - pt1.z)
        axis_dir.normalize()

        n_before = get_face_normal(new_bodies[k].faces.item(0))
        n_after = get_face_normal(new_bodies[k + 1].faces.item(0))

        angle = signed_angle_about_axis(n_after, n_before, axis_dir)

        if abs(angle) > 1e-6:
            rotMatrix = adsk.core.Matrix3D.create()
            rotMatrix.setToRotation(angle, axis_dir, axis_point)

            coll = adsk.core.ObjectCollection.create()
            for b in new_bodies[k + 1:]:
                coll.add(b)
            moveInput = moveFeats.createInput(coll, rotMatrix)
            moveFeats.add(moveInput)

    return new_bodies
