/* Copyright (c) 2015-2017, EPFL/Blue Brain Project
 * All rights reserved. Do not distribute without permission.
 * Responsible Author: Cyrille Favreau <cyrille.favreau@epfl.ch>
 *
 * This file is part of Brayns <https://github.com/BlueBrain/Brayns>
 *
 * This library is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License version 3.0 as published
 * by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "MorphologyLoader.h"

#include <brayns/common/geometry/Cone.h>
#include <brayns/common/geometry/Cylinder.h>
#include <brayns/common/geometry/Sphere.h>
#include <brayns/common/log.h>
#include <brayns/common/scene/Scene.h>
#include <brayns/common/simulation/CircuitSimulationHandler.h>
#include <brayns/io/algorithms/MetaballsGenerator.h>

#include <algorithm>

#ifdef BRAYNS_USE_BRION
#include <brain/brain.h>
#include <brion/brion.h>
#endif

#ifdef EXPORT_TO_FILE
namespace
{
const float factor = 1.f;
const float power = 4.f;
}
#endif

namespace brayns
{
MorphologyLoader::MorphologyLoader(const GeometryParameters &geometryParameters)
    : _geometryParameters(geometryParameters)
{
}

#ifdef BRAYNS_USE_BRION

#ifdef EXPORT_TO_FILE
void _writeToFile(std::ofstream &outputFile, SpheresMap &spheres,
                  const size_t material, Boxf &bounds, const Vector3f &position,
                  const float radius)
{
    float f = position.x();
    outputFile.write((char *)&f, sizeof(float));
    f = position.y();
    outputFile.write((char *)&f, sizeof(float));
    f = position.z();
    outputFile.write((char *)&f, sizeof(float));
    f = radius;
    outputFile.write((char *)&f, sizeof(float));
    f = 1.f;
    outputFile.write((char *)&f, sizeof(float));
    spheres[material].push_back(
        SpherePtr(new Sphere(material, position, radius, 0.f, 0.f)));
    bounds.merge(position);
}

void _createSpheres(std::ofstream &outputFile, SpheresMap &spheres,
                    const size_t material, Boxf &bounds, const Vector3f &from,
                    const float r1, const Vector3f &to, const float r2)
{
    Vector3f dir = to - from;
    const float d = dir.length();
    dir = normalize(dir);
    float correction = 1.f;
    float pos, signPos, stopPos;

    if (7 / 8 * (r1 + r2) > d)
        correction =
            std::pow((d * 0.28f / (r2 + r1) / 2.f) + 0.5f, 1.f / power);

    const float R1 = correction * r1;
    const float R2 = correction * r2;

    const float Rmax = std::max(R1, R2);
    const float Rmin = std::min(R1, R2);

    if (R1 > R2)
    {
        pos = 0;
        stopPos = d - R2 / factor;
        signPos = 1.0;
    }
    else
    {
        pos = d;
        stopPos = R1 / factor;
        signPos = -1.0;
    }

    _writeToFile(outputFile, spheres, MATERIAL_AFFERENT_SYNAPSE, bounds, from,
                 R1);

    if (7 / 8 * (Rmax + Rmin) > d)
    {
    }
    else if (Rmax + Rmin > d)
    {
        const float R = (Rmax + Rmin) / 2.f;
        pos = d / 2;
        const Vector3f P = from + pos * dir;
        _writeToFile(outputFile, spheres, material, bounds, P, R);
    }
    else
    {
        const float scale_initial = 1.f - (Rmax - Rmin) / (d * factor - Rmin);
        float delta_e = 0.f;
        if (Rmax != Rmin)
        {
            const float number =
                std::log(Rmin / Rmax) / std::log(scale_initial);
            const float error = Rmax *
                                (std::pow(scale_initial, std::floor(number)) -
                                 std::pow(scale_initial, number)) /
                                (factor * (1 - scale_initial));
            delta_e = error / std::floor(number);
        }
        size_t i = 1;
        float scale_pos = 1;
        float scale_radius = scale_initial;
        pos = pos + signPos * scale_pos * Rmax / factor;
        while (signPos * pos <= signPos * stopPos)
        {
            const Vector3f P = from + pos * dir;
            const float R = scale_radius * Rmax;
            _writeToFile(outputFile, spheres, material, bounds, P, R);

            scale_pos = std::pow(scale_initial, i);
            scale_radius = std::pow(scale_initial, i + 1);
            pos = pos + signPos * (scale_pos * Rmax / factor + delta_e);
            ++i;
        }
    }

    _writeToFile(outputFile, spheres, MATERIAL_EFFERENT_SYNAPSE, bounds, to,
                 R2);
}
#endif

brain::neuron::SectionTypes _getSectionTypes(
    const size_t morphologySectionTypes)
{
    brain::neuron::SectionTypes sectionTypes;
    if (morphologySectionTypes & MST_SOMA)
        sectionTypes.push_back(brain::neuron::SectionType::soma);
    if (morphologySectionTypes & MST_AXON)
        sectionTypes.push_back(brain::neuron::SectionType::axon);
    if (morphologySectionTypes & MST_DENDRITE)
        sectionTypes.push_back(brain::neuron::SectionType::dendrite);
    if (morphologySectionTypes & MST_APICAL_DENDRITE)
        sectionTypes.push_back(brain::neuron::SectionType::apicalDendrite);
    return sectionTypes;
}

bool MorphologyLoader::_importMorphologyAsMesh(const servus::URI &source,
                                               const size_t morphologyIndex,
                                               const MaterialsMap &materials,
                                               const Matrix4f &transformation,
                                               TrianglesMeshMap &meshes,
                                               Boxf &bounds)
{
    try
    {
        const size_t morphologySectionTypes =
            _geometryParameters.getMorphologySectionTypes();

        brain::neuron::Morphology morphology(source, transformation);
        brain::neuron::SectionTypes sectionTypes =
            _getSectionTypes(morphologySectionTypes);

        const brain::neuron::Sections &sections =
            morphology.getSections(sectionTypes);

        Spheres metaballs;

        if (morphologySectionTypes & MST_SOMA)
        {
            // Soma
            const brain::neuron::Soma &soma = morphology.getSoma();
            const size_t material = _getMaterialFromSectionType(
                morphologyIndex, size_t(brain::neuron::SectionType::soma));
            const Vector3f center = soma.getCentroid();

            const float radius =
                (_geometryParameters.getRadiusCorrection() != 0.f
                     ? _geometryParameters.getRadiusCorrection()
                     : soma.getMeanRadius() *
                           _geometryParameters.getRadiusMultiplier());

            metaballs.push_back(
                SpherePtr(new Sphere(material, center, radius, 0.f, 0.f)));
            bounds.merge(center);
        }

        // Dendrites and axon
        for (size_t s = 0; s < sections.size(); ++s)
        {
            const auto &section = sections[s];
            const bool hasParent = section.hasParent();
            if (hasParent)
            {
                const auto parentSectionType = section.getParent().getType();
                if (parentSectionType != brain::neuron::SectionType::soma)
                    continue;
            }

            const auto material =
                _getMaterialFromSectionType(morphologyIndex,
                                            size_t(section.getType()));
            const auto &samples = section.getSamples();
            if (samples.empty())
                continue;

            const size_t samplesFromSoma =
                _geometryParameters.getMetaballsSamplesFromSoma();
            const size_t samplesToProcess =
                std::min(samplesFromSoma, samples.size());
            for (size_t i = 0; i < samplesToProcess; ++i)
            {
                const auto &sample = samples[i];
                const Vector3f position(sample.x(), sample.y(), sample.z());
                const auto radius =
                    (_geometryParameters.getRadiusCorrection() != 0.f
                         ? _geometryParameters.getRadiusCorrection()
                         : sample.w() * 0.5f *
                               _geometryParameters.getRadiusMultiplier());

                if (radius > 0.f)
                    metaballs.push_back(SpherePtr(
                        new Sphere(material, position, radius, 0.f, 0.f)));

                bounds.merge(position);
            }
        }

        // Generate mesh
        const size_t gridSize = _geometryParameters.getMetaballsGridSize();
        const float threshold = _geometryParameters.getMetaballsThreshold();
        MetaballsGenerator metaballsGenerator;
        const size_t material = _getMaterialFromSectionType(
            morphologyIndex, size_t(brain::neuron::SectionType::soma));
        metaballsGenerator.generateMesh(metaballs, gridSize, threshold,
                                        materials, material, meshes);
    }
    catch (const std::runtime_error &e)
    {
        BRAYNS_ERROR << e.what() << std::endl;
        return false;
    }
    return true;
}

#ifdef EXPORT_TO_FILE
void _createSpines(std::ofstream &outputFile, const brain::Circuit &circuit,
                   const brain::GIDSet &gids, const size_t gid,
                   const float radius, SpheresMap &spheres, CylindersMap &,
                   Boxf &bounds)
#else
void _createSpines(const brain::Circuit &circuit, const brain::GIDSet &gids,
                   const size_t gid, const float radius, SpheresMap &spheres,
                   CylindersMap &cylinders, Boxf &bounds)
#endif
{
    return;
    BRAYNS_INFO << "Create spines" << std::endl;

    // Afferent synapses
    brain::SynapsesStream afferentSynapses = circuit.getAfferentSynapses(gids);
    std::future<brain::Synapses> future = afferentSynapses.read();

    size_t i = 0;
    while (!afferentSynapses.eos())
    {
        const brain::Synapses synapses = future.get();
        future = afferentSynapses.read(); // fetch next
        for (const auto &synapse : synapses)
        {
            if (synapse.getPostsynapticSectionID() != 0 &&
                synapse.getPostsynapticGID() == gid)
            {
                const Vector3f from = synapse.getPresynapticSurfacePosition();
                const Vector3f to = synapse.getPostsynapticSurfacePosition();
                bounds.merge(from);
                bounds.merge(to);
#ifdef EXPORT_TO_FILE
                Vector3f dir = (to - from) / 10.f;
                float r = (2.f * radius) / 10.f;
                for (float b = 0.f; b < 10.f; b += 1.f)
                    _writeToFile(outputFile, spheres, MATERIAL_AFFERENT_SYNAPSE,
                                 bounds, from + dir * b, 4.f * radius - r * b);
#else
                spheres[MATERIAL_AFFERENT_SYNAPSE].push_back(
                    SpherePtr(new Sphere(MATERIAL_AFFERENT_SYNAPSE, from,
                                         4.f * radius, 0.f, 0.f)));
                cylinders[MATERIAL_AFFERENT_SYNAPSE].push_back(
                    CylinderPtr(new Cylinder(MATERIAL_AFFERENT_SYNAPSE, from,
                                             to, 2.f * radius, 0.f, 0.f)));
#endif
            }
            ++i;
        }
    }
    BRAYNS_INFO << "Added " << i << " afferent synapses" << std::endl;

    // Afferent synapses
    brain::SynapsesStream efferentSynapses = circuit.getEfferentSynapses(gids);
    future = efferentSynapses.read();

    i = 0;
    while (!efferentSynapses.eos())
    {
        const brain::Synapses synapses = future.get();
        future = efferentSynapses.read(); // fetch next
        for (const auto &synapse : synapses)
        {
            if (synapse.getPostsynapticSectionID() != 0 &&
                synapse.getPostsynapticGID() == gid)
            {
                const Vector3f from = synapse.getPostsynapticSurfacePosition();
                const Vector3f to = synapse.getPresynapticSurfacePosition();
                bounds.merge(from);
                bounds.merge(to);
#ifdef EXPORT_TO_FILE
                Vector3f dir = (to - from) / 10.f;
                float r = (2.f * radius) / 10.f;
                for (float b = 0; b < 10.f; b += 1.f)
                    _writeToFile(outputFile, spheres, MATERIAL_EFFERENT_SYNAPSE,
                                 bounds, from + dir * b, 2.f * radius + r * b);
#else
                spheres[MATERIAL_EFFERENT_SYNAPSE].push_back(
                    SpherePtr(new Sphere(MATERIAL_EFFERENT_SYNAPSE, from,
                                         4.f * radius, 0.f, 0.f)));
                cylinders[MATERIAL_EFFERENT_SYNAPSE].push_back(
                    CylinderPtr(new Cylinder(MATERIAL_EFFERENT_SYNAPSE, from,
                                             to, 1.5f * radius, 0.f, 0.f)));
#endif
            }
            ++i;
        }
    }
    BRAYNS_INFO << "Added " << i << " efferent synapses" << std::endl;
}

bool MorphologyLoader::importMorphology(const servus::URI &uri,
                                        const int morphologyIndex, Scene &scene)
{
#ifdef EXPORT_TO_FILE
    std::stringstream s;
    s << uri << ".bin";
    std::ofstream outputFile;
    outputFile.open(s.str(), std::ios::out | std::ios::binary);
#endif

    bool returnValue = true;
    if (_geometryParameters.useMetaballs())
    {
        returnValue =
            _importMorphologyAsMesh(uri, morphologyIndex, scene.getMaterials(),
                                    Matrix4f(), scene.getTriangleMeshes(),
                                    scene.getWorldBounds());
    }
    float maxDistanceToSoma;
    float minRadius;
#ifdef EXPORT_TO_FILE
    returnValue = returnValue &&
                  _importMorphology(uri, morphologyIndex, Matrix4f(), nullptr,
                                    scene.getSpheres(), scene.getCylinders(),
                                    scene.getCones(), scene.getWorldBounds(), 0,
                                    maxDistanceToSoma, minRadius, outputFile);
    outputFile.close();
#else
    returnValue = returnValue &&
                  _importMorphology(uri, morphologyIndex, Matrix4f(), nullptr,
                                    scene.getSpheres(), scene.getCylinders(),
                                    scene.getCones(), scene.getWorldBounds(), 0,
                                    maxDistanceToSoma, minRadius);
#endif
    return returnValue;
}

#ifdef EXPORT_TO_FILE
bool MorphologyLoader::_importMorphology(
    const servus::URI &source, const size_t morphologyIndex,
    const Matrix4f &transformation,
    const SimulationInformation *simulationInformation, SpheresMap &spheres,
    CylindersMap &cylinders, ConesMap &cones, Boxf &bounds,
    const size_t simulationOffset, float &maxDistanceToSoma, float &minRadius,
    std::ofstream &outputFile)
#else
bool MorphologyLoader::_importMorphology(
    const servus::URI &source, const size_t morphologyIndex,
    const Matrix4f &transformation,
    const SimulationInformation *simulationInformation, SpheresMap &spheres,
    CylindersMap &cylinders, ConesMap &cones, Boxf &bounds,
    const size_t simulationOffset, float &maxDistanceToSoma, float &minRadius)
#endif
{
    Vector3f somaPosition;
    maxDistanceToSoma = 0.f;
    try
    {
        Vector3f translation = {0.f, 0.f, 0.f};

        brain::neuron::Morphology morphology(source, transformation);
        brain::neuron::SectionTypes sectionTypes;

        const MorphologyLayout &layout =
            _geometryParameters.getMorphologyLayout();

        if (layout.nbColumns != 0)
        {
            Boxf morphologyAABB;
            const brain::Vector4fs &points = morphology.getPoints();
            for (Vector4f point : points)
            {
                const Vector3f p = {point.x(), point.y(), point.z()};
                morphologyAABB.merge(p);
            }

            const Vector3f positionInGrid = {
                -layout.horizontalSpacing *
                    static_cast<float>(morphologyIndex % layout.nbColumns),
                -layout.verticalSpacing *
                    static_cast<float>(morphologyIndex / layout.nbColumns),
                0.f};
            translation = positionInGrid - morphologyAABB.getCenter();
        }

        const size_t morphologySectionTypes =
            _geometryParameters.getMorphologySectionTypes();

        sectionTypes = _getSectionTypes(morphologySectionTypes);

        const brain::neuron::Sections &sections =
            morphology.getSections(sectionTypes);

        size_t sectionId = 0;

        float offset = 0.f;
        if (simulationInformation)
            offset = (*simulationInformation->compartmentOffsets)[sectionId];
        else if (simulationOffset != 0)
            offset = simulationOffset;

#ifdef EXPORT_TO_FILE
        {
            const size_t material = _getMaterialFromSectionType(
                morphologyIndex, size_t(brain::neuron::SectionType::soma));
            const brain::neuron::Soma &soma = morphology.getSoma();
            somaPosition = soma.getCentroid();
            const float somaRadius = soma.getMeanRadius();
            _writeToFile(outputFile, spheres, material, bounds, somaPosition,
                         somaRadius / 2.f);

            // Profile
            const auto &profilePoints = soma.getProfilePoints();
            const float innerSphereRadius =
                2.f * somaRadius / profilePoints.size();

            if (false)
            {
                BRAYNS_INFO << "Adding " << profilePoints.size()
                            << " profile points" << std::endl;

                // Filled profile
                for (const auto &profilePoint : profilePoints)
                {
                    const Vector3f point = {profilePoint.x(), profilePoint.y(),
                                            profilePoint.z()};
                    const size_t nbBalls = 8;
                    const Vector3f dir = point - somaPosition;
                    const Vector3f step = dir / float(nbBalls);
                    for (size_t j = 1; j < nbBalls; ++j)
                    {
                        const Vector3f center = somaPosition + step * j;
                        _writeToFile(outputFile, spheres, material, bounds,
                                     center, innerSphereRadius);
                    }
                }
            }

            // Children
            const auto &children = soma.getChildren();
            for (const auto &child : children)
            {
                const auto &samples = child.getSamples();
                const Vector3f sample = {samples[0].x(), samples[0].y(),
                                         samples[0].z()};
                _createSpheres(outputFile, spheres, material, bounds,
                               somaPosition,
                               somaRadius * 0.5f *
                                   _geometryParameters.getRadiusMultiplier(),
                               sample,
                               samples[0].w() * 0.5f *
                                   _geometryParameters.getRadiusMultiplier());
            }
        }
#else
        if (!_geometryParameters.useMetaballs() &&
            morphologySectionTypes & MST_SOMA)
        {
            // Soma
            const brain::neuron::Soma &soma = morphology.getSoma();
            const size_t material = _getMaterialFromSectionType(
                morphologyIndex, size_t(brain::neuron::SectionType::soma));
            const Vector3f &center = soma.getCentroid() + translation;

            const float radius =
                (_geometryParameters.getRadiusCorrection() != 0.f
                     ? _geometryParameters.getRadiusCorrection()
                     : soma.getMeanRadius() * 0.5f *
                           _geometryParameters.getRadiusMultiplier());

            spheres[material].push_back(
                SpherePtr(new Sphere(material, center, radius, 0.f, offset)));
            bounds.merge(center);
        }
#endif

        // Dendrites and axon
        for (const auto &section : sections)
        {
            const size_t material =
                _getMaterialFromSectionType(morphologyIndex,
                                            size_t(section.getType()));
            const Vector4fs &samples = section.getSamples();
            if (samples.size() < 1)
                continue;

#ifdef EXPORT_TO_FILE
            if (samples.size() < 2)
                continue;

            // BRANCHES
            for (size_t s = 1; s < samples.size(); ++s)
            {
                const Vector3f origin = {samples[s - 1].x(), samples[s - 1].y(),
                                         samples[s - 1].z()};
                const Vector3f target = {samples[s].x(), samples[s].y(),
                                         samples[s].z()};

                maxDistanceToSoma =
                    std::max(maxDistanceToSoma, section.getDistanceToSoma());

                const float originRadius =
                    samples[s - 1].w() * 0.5f *
                    _geometryParameters.getRadiusMultiplier();
                const float targetRadius =
                    samples[s].w() * 0.5f *
                    _geometryParameters.getRadiusMultiplier();
                _createSpheres(outputFile, spheres, material, bounds, origin,
                               originRadius, target, targetRadius);
            }
            continue;
#endif

            Vector4f previousSample = samples[0];
            size_t step = 1;
            switch (_geometryParameters.getGeometryQuality())
            {
            case GeometryQuality::low:
                step = samples.size() - 1;
                break;
            case GeometryQuality::medium:
                step = samples.size() / 2;
                step = (step == 0) ? 1 : step;
                break;
            default:
                step = 1;
            }

            const float distanceToSoma = section.getDistanceToSoma();
            const floats &distancesToSoma = section.getSampleDistancesToSoma();

            float segmentStep = 0.f;
            if (simulationInformation)
            {
                const auto &counts = *simulationInformation->compartmentCounts;
                // Number of compartments usually differs from number of samples
                if (samples.empty() && counts[sectionId] > 1)
                    segmentStep = counts[sectionId] / float(samples.size());
            }

            bool done = false;
            for (size_t i = step; !done && i < samples.size() + step; i += step)
            {
                if (i >= samples.size())
                {
                    i = samples.size() - 1;
                    done = true;
                }

                const float distance = distanceToSoma + distancesToSoma[i];

                maxDistanceToSoma = std::max(maxDistanceToSoma, distance);

                if (simulationInformation)
                    offset = (*simulationInformation
                                   ->compartmentOffsets)[sectionId] +
                             float(i) * segmentStep;
                else if (simulationOffset != 0)
                    offset = simulationOffset + distance;

                Vector4f sample = samples[i];
                const float previousRadius =
                    (_geometryParameters.getRadiusCorrection() != 0.f
                         ? _geometryParameters.getRadiusCorrection()
                         : samples[i - step].w() * 0.5f *
                               _geometryParameters.getRadiusMultiplier());

                Vector3f position(sample.x(), sample.y(), sample.z());
                position += translation;
                Vector3f target(previousSample.x(), previousSample.y(),
                                previousSample.z());
                target += translation;
                const float radius =
                    (_geometryParameters.getRadiusCorrection() != 0.f
                         ? _geometryParameters.getRadiusCorrection()
                         : samples[i].w() * 0.5f *
                               _geometryParameters.getRadiusMultiplier());
                minRadius = std::min(minRadius, radius);

                if (radius > 0.f)
                    spheres[material].push_back(
                        SpherePtr(new Sphere(material, position, radius,
                                             distance, offset)));

                bounds.merge(position);
                if (position != target && radius > 0.f && previousRadius > 0.f)
                {
                    if (radius == previousRadius)
                        cylinders[material].push_back(CylinderPtr(
                            new Cylinder(material, position, target, radius,
                                         distance, offset)));
                    else
                        cones[material].push_back(ConePtr(
                            new Cone(material, position, target, radius,
                                     previousRadius, distance, offset)));
                    bounds.merge(target);
                }
                previousSample = sample;
            }
            ++sectionId;
        }
        BRAYNS_DEBUG << "Soma position       : " << somaPosition << std::endl;
        BRAYNS_DEBUG << "Normalized position : "
                     << (somaPosition - bounds.getMin()) / bounds.getSize()
                     << std::endl;
        BRAYNS_DEBUG << "Distance to soma    : " << maxDistanceToSoma
                     << std::endl;
    }
    catch (const std::runtime_error &e)
    {
        BRAYNS_ERROR << e.what() << std::endl;
        return false;
    }
    return true;
}

bool MorphologyLoader::importCircuit(const servus::URI &circuitConfig,
                                     const std::string &target, Scene &scene)
{
#ifdef EXPORT_TO_FILE
    std::stringstream circuitFilename;
    circuitFilename << circuitConfig << "_" << target << ".bin";
    std::ofstream outputFile;
    outputFile.open(circuitFilename.str(), std::ios::out | std::ios::binary);
#endif

    const std::string &filename = circuitConfig.getPath();
    const brion::BlueConfig bc(filename);
    const brain::Circuit circuit(bc);

    brain::GIDSet gids =
        (target.empty() ? circuit.getGIDs() : circuit.getGIDs(target));

    const auto gid = _geometryParameters.getNeuronGID();
    if (gid != std::numeric_limits<uint64_t>::max())
    {
        gids.clear();
        gids.insert(gid);
    }

    if (gids.empty())
    {
        BRAYNS_ERROR << "Circuit does not contain any cells" << std::endl;
        return false;
    }
    const Matrix4fs &transforms = circuit.getTransforms(gids);

    const brain::URIs &uris = circuit.getMorphologyURIs(gids);

    BRAYNS_INFO << "Loading " << uris.size() << " cells" << std::endl;

    std::map<size_t, float> morphologyOffsets;

    size_t simulationOffset = 1;
    size_t simulatedCells = 0;
    size_t progress = 0;
    float minRadius = std::numeric_limits<float>::max();

#pragma omp parallel
    {
        SpheresMap private_spheres;
        CylindersMap private_cylinders;
        ConesMap private_cones;
        Boxf private_bounds;
#pragma omp for nowait
        for (size_t i = 0; i < uris.size(); ++i)
        {
            const auto &uri = uris[i];

            float maxDistanceToSoma = 0.f;

            if (_geometryParameters.useMetaballs())
            {
                _importMorphologyAsMesh(uri, i, scene.getMaterials(),
                                        transforms[i],
                                        scene.getTriangleMeshes(),
                                        scene.getWorldBounds());
            }

#ifdef EXPORT_TO_FILE
            if (_importMorphology(uri, i, transforms[i], 0, private_spheres,
                                  private_cylinders, private_cones,
                                  private_bounds, simulationOffset,
                                  maxDistanceToSoma, minRadius, outputFile))
#else
            if (_importMorphology(uri, i, transforms[i], 0, private_spheres,
                                  private_cylinders, private_cones,
                                  private_bounds, simulationOffset,
                                  maxDistanceToSoma, minRadius))
#endif
            {
                morphologyOffsets[simulatedCells] = maxDistanceToSoma;
                simulationOffset += maxDistanceToSoma;
            }

            BRAYNS_PROGRESS(progress, uris.size());
#pragma omp atomic
            ++progress;
        }

#pragma omp critical
        for (const auto &p : private_spheres)
        {
            const size_t material = p.first;
            scene.getSpheres()[material].insert(
                scene.getSpheres()[material].end(),
                private_spheres[material].begin(),
                private_spheres[material].end());
        }

#pragma omp critical
        for (const auto &p : private_cylinders)
        {
            const size_t material = p.first;
            scene.getCylinders()[material].insert(
                scene.getCylinders()[material].end(),
                private_cylinders[material].begin(),
                private_cylinders[material].end());
        }

#pragma omp critical
        for (const auto &p : private_cones)
        {
            const size_t material = p.first;
            scene.getCones()[material].insert(scene.getCones()[material].end(),
                                              private_cones[material].begin(),
                                              private_cones[material].end());
        }

        scene.getWorldBounds().merge(private_bounds);
    }

// Spines
// gids = (target.empty() ? circuit.getGIDs() : circuit.getGIDs(target));
//_createSpines(outputFile, circuit, gids, gid, minRadius, scene.getSpheres(),
//              scene.getCylinders(), scene.getWorldBounds());
#ifdef EXPORT_TO_FILE
    outputFile.close();
#endif
    return true;
}

bool MorphologyLoader::importCircuit(const servus::URI &circuitConfig,
                                     const std::string &target,
                                     const std::string &report, Scene &scene)
{
    const std::string &filename = circuitConfig.getPath();
    const brion::BlueConfig bc(filename);
    const brain::Circuit circuit(bc);
    const brain::GIDSet &gids =
        (target.empty() ? circuit.getGIDs() : circuit.getGIDs(target));
    if (gids.empty())
    {
        BRAYNS_ERROR << "Circuit does not contain any cells" << std::endl;
        return false;
    }

    const Matrix4fs &transforms = circuit.getTransforms(gids);

    const brain::URIs &uris = circuit.getMorphologyURIs(gids);

    // Load simulation information from compartment reports
    const brion::CompartmentReport compartmentReport(
        brion::URI(bc.getReportSource(report).getPath()), brion::MODE_READ,
        gids);

    const brion::CompartmentCounts &compartmentCounts =
        compartmentReport.getCompartmentCounts();

    const brion::SectionOffsets &compartmentOffsets =
        compartmentReport.getOffsets();

    brain::URIs cr_uris;
    const brain::GIDSet &cr_gids = compartmentReport.getGIDs();

    BRAYNS_INFO << "Loading " << cr_gids.size() << " simulated cells"
                << std::endl;
    for (const auto cr_gid : cr_gids)
    {
        auto it = std::find(gids.begin(), gids.end(), cr_gid);
        auto index = std::distance(gids.begin(), it);
        cr_uris.push_back(uris[index]);
    }

    size_t progress = 0;
    float minRadius = std::numeric_limits<float>::max();

#pragma omp parallel
    {
        SpheresMap private_spheres;
        CylindersMap private_cylinders;
        ConesMap private_cones;
        Boxf private_bounds;
#pragma omp for nowait
        for (size_t i = 0; i < cr_uris.size(); ++i)
        {
            const auto &uri = cr_uris[i];
#ifdef EXPORT_TO_FILE
            std::stringstream morphologyFilename;
            morphologyFilename << uri.getPath() << ".bin";
            BRAYNS_INFO << "Creating  " << morphologyFilename.str()
                        << std::endl;
            std::ofstream outputFile;
            outputFile.open(morphologyFilename.str(),
                            std::ios::out | std::ios::binary);
#endif
            const SimulationInformation simulationInformation = {
                &compartmentCounts[i], &compartmentOffsets[i]};

            if (_geometryParameters.useMetaballs())
            {
                _importMorphologyAsMesh(uri, i, scene.getMaterials(),
                                        transforms[i],
                                        scene.getTriangleMeshes(),
                                        scene.getWorldBounds());
            }
            float maxDistanceToSoma;
#ifdef EXPORT_TO_FILE
            _importMorphology(uri, i, transforms[i], &simulationInformation,
                              private_spheres, private_cylinders, private_cones,
                              private_bounds, 0, maxDistanceToSoma, minRadius,
                              outputFile);
            outputFile.close();
#else
            _importMorphology(uri, i, transforms[i], &simulationInformation,
                              private_spheres, private_cylinders, private_cones,
                              private_bounds, 0, maxDistanceToSoma, minRadius);
#endif
            BRAYNS_PROGRESS(progress, cr_uris.size());
#pragma omp atomic
            ++progress;
        }

#pragma omp critical
        for (const auto &p : private_spheres)
        {
            const size_t material = p.first;
            scene.getSpheres()[material].insert(
                scene.getSpheres()[material].end(),
                private_spheres[material].begin(),
                private_spheres[material].end());
        }

#pragma omp critical
        for (const auto &p : private_cylinders)
        {
            const size_t material = p.first;
            scene.getCylinders()[material].insert(
                scene.getCylinders()[material].end(),
                private_cylinders[material].begin(),
                private_cylinders[material].end());
        }

#pragma omp critical
        for (const auto &p : private_cones)
        {
            const size_t material = p.first;
            scene.getCones()[material].insert(scene.getCones()[material].end(),
                                              private_cones[material].begin(),
                                              private_cones[material].end());
        }

        scene.getWorldBounds().merge(private_bounds);
    }

    size_t nonSimulatedCells = _geometryParameters.getNonSimulatedCells();
    if (nonSimulatedCells != 0)
    {
        // Non simulated cells
        const brain::GIDSet &allGids = circuit.getGIDs();
        const brain::URIs &allUris = circuit.getMorphologyURIs(allGids);
        const Matrix4fs &allTransforms = circuit.getTransforms(allGids);

        cr_uris.clear();
        size_t index = 0;
        for (const auto gid : allGids)
        {
            auto it = std::find(cr_gids.begin(), cr_gids.end(), gid);
            if (it == cr_gids.end())
                cr_uris.push_back(allUris[index]);
            ++index;
        }

        if (cr_uris.size() < nonSimulatedCells)
            nonSimulatedCells = cr_uris.size();

        BRAYNS_INFO << "Loading " << nonSimulatedCells << " non-simulated cells"
                    << std::endl;

        progress = 0;
#pragma omp parallel
        {
            SpheresMap private_spheres;
            CylindersMap private_cylinders;
            ConesMap private_cones;
            Boxf private_bounds;
#pragma omp for nowait
            for (size_t i = 0; i < nonSimulatedCells; ++i)
            {
                float maxDistanceToSoma;
                const auto &uri = allUris[i];

#ifdef EXPORT_TO_FILE
                std::stringstream morphologyFilename;
                morphologyFilename << uri.getPath() << ".bin";
                BRAYNS_INFO << "Creating  " << morphologyFilename.str()
                            << std::endl;
                std::ofstream outputFile;
                outputFile.open(morphologyFilename.str(),
                                std::ios::out | std::ios::binary);
                _importMorphology(uri, i, allTransforms[i], 0, private_spheres,
                                  private_cylinders, private_cones,
                                  private_bounds, 0, maxDistanceToSoma,
                                  minRadius, outputFile);

                outputFile.close();
#else
                _importMorphology(uri, i, allTransforms[i], 0, private_spheres,
                                  private_cylinders, private_cones,
                                  private_bounds, 0, maxDistanceToSoma,
                                  minRadius);
#endif
                BRAYNS_PROGRESS(progress, allUris.size());
#pragma omp atomic
                ++progress;
            }

#pragma omp critical
            for (const auto &p : private_spheres)
            {
                const size_t material = p.first;
                scene.getSpheres()[material].insert(
                    scene.getSpheres()[material].end(),
                    private_spheres[material].begin(),
                    private_spheres[material].end());
            }

#pragma omp critical
            for (const auto &p : private_cylinders)
            {
                const size_t material = p.first;
                scene.getCylinders()[material].insert(
                    scene.getCylinders()[material].end(),
                    private_cylinders[material].begin(),
                    private_cylinders[material].end());
            }

#pragma omp critical
            for (const auto &p : private_cones)
            {
                const size_t material = p.first;
                scene.getCones()[material].insert(
                    scene.getCones()[material].end(),
                    private_cones[material].begin(),
                    private_cones[material].end());
            }

            scene.getWorldBounds().merge(private_bounds);
        }
    }

    // Spines
    //_createSpines(outputFile, circuit, gids, 0, minRadius, scene.getSpheres(),
    //              scene.getCylinders(), scene.getWorldBounds());
    return true;
}

bool MorphologyLoader::importSimulationData(const servus::URI &circuitConfig,
                                            const std::string &target,
                                            const std::string &report,
                                            Scene &scene)
{
    const std::string &filename = circuitConfig.getPath();
    const brion::BlueConfig bc(filename);
    const brain::Circuit circuit(bc);
    const brain::GIDSet &gids =
        (target.empty() ? circuit.getGIDs() : circuit.getGIDs(target));
    if (gids.empty())
    {
        BRAYNS_ERROR << "Circuit does not contain any cells" << std::endl;
        return false;
    }

    // Load simulation information from compartment reports
    brion::CompartmentReport compartmentReport(
        brion::URI(bc.getReportSource(report).getPath()), brion::MODE_READ,
        gids);

    CircuitSimulationHandlerPtr simulationHandler(
        new CircuitSimulationHandler(_geometryParameters));
    scene.setSimulationHandler(simulationHandler);
    const std::string &cacheFile = _geometryParameters.getSimulationCacheFile();
    if (simulationHandler->attachSimulationToCacheFile(cacheFile))
        // Cache already exists, no need to create it.
        return true;

    BRAYNS_INFO << "Cache file does not exist, creating it" << std::endl;
    std::ofstream file(cacheFile, std::ios::out | std::ios::binary);

    if (!file.is_open())
    {
        BRAYNS_ERROR << "Failed to create cache file" << std::endl;
        return false;
    }

    // Load simulation information from compartment reports
    const float start = compartmentReport.getStartTime();
    const float end = compartmentReport.getEndTime();
    const float step = compartmentReport.getTimestep();

    const float firstFrame =
        std::max(start, _geometryParameters.getStartSimulationTime());
    const float lastFrame =
        std::min(end, _geometryParameters.getEndSimulationTime());
    const uint64_t frameSize = compartmentReport.getFrameSize();

    const uint64_t nbFrames = (lastFrame - firstFrame) / step;

    BRAYNS_INFO
        << "Loading values from compartment report and saving them to cache"
        << std::endl;

    // Write header
    simulationHandler->setNbFrames(nbFrames);
    simulationHandler->setFrameSize(frameSize);
    simulationHandler->writeHeader(file);

    // Write body
    for (uint64_t frame = 0; frame < nbFrames; ++frame)
    {
        BRAYNS_PROGRESS(frame, nbFrames);
        const float frameTime = firstFrame + step * frame;
        const brion::floatsPtr &valuesPtr =
            compartmentReport.loadFrame(frameTime);
        const floats &values = *valuesPtr;
        simulationHandler->writeFrame(file, values);

        if (false)
            _createSimulationVolumeFromCircuit(circuitConfig, target, report,
                                               frame, values);
    }
    file.close();

    BRAYNS_INFO << "----------------------------------------" << std::endl;
    BRAYNS_INFO << "Cache file successfully created" << std::endl;
    BRAYNS_INFO << "Number of frames: " << nbFrames << std::endl;
    BRAYNS_INFO << "Frame size      : " << frameSize << std::endl;
    BRAYNS_INFO << "----------------------------------------" << std::endl;
    return true;
}

void MorphologyLoader::_writeToVolume(floats &volume,
                                      const Vector3f &volumeSize,
                                      const Vector3f &position,
                                      const float value)
{
    const Vector3f minAABB(-903.366f, -404.412f, -1319.39f);
    const Vector3f maxAABB(1267.04f, 2232.13f, 1243.36f);

    const Vector3f size = (maxAABB - minAABB) / volumeSize;
    const Vector3f relativePosition = (position - minAABB) / size;

    const int64_t index =
        relativePosition.x() + relativePosition.y() * volumeSize.x() +
        relativePosition.z() * volumeSize.x() * volumeSize.y();
    if (index >= 0 && index < volumeSize.x() * volumeSize.y() * volumeSize.z())
        volume[index] = value;
    else
        BRAYNS_ERROR << index << std::endl;
}

bool MorphologyLoader::_importSimulationFromMorphology(
    floats &volume, const Vector3ui &volumeSize, const servus::URI &source,
    const Matrix4f &transformation,
    const SimulationInformation *simulationInformation, const floats &values)
{
    BRAYNS_INFO << "_importSimulationFromMorphology: " << source << std::endl;
    try
    {
        Vector3f translation = {0.f, 0.f, 0.f};

        brain::neuron::Morphology morphology(source, transformation);
        brain::neuron::SectionTypes sectionTypes;
        sectionTypes.push_back(brain::neuron::SectionType::soma);
        sectionTypes.push_back(brain::neuron::SectionType::axon);
        sectionTypes.push_back(brain::neuron::SectionType::dendrite);
        sectionTypes.push_back(brain::neuron::SectionType::apicalDendrite);

        const brain::neuron::Sections &sections =
            morphology.getSections(sectionTypes);

        size_t sectionId = 0;
        float offset = 0.f;
        if (simulationInformation)
            offset = (*simulationInformation->compartmentOffsets)[sectionId];
        else
            offset = 0;

        // Dendrites and axon
        for (const auto &section : sections)
        {
            const Vector4fs &samples = section.getSamples();
            if (samples.size() < 1)
                continue;

            Vector4f previousSample = samples[0];

            float segmentStep = 0.f;
            if (simulationInformation)
            {
                const auto &counts = *simulationInformation->compartmentCounts;
                // Number of compartments usually differs from number of samples
                if (samples.empty() && counts[sectionId] > 1)
                    segmentStep = counts[sectionId] / float(samples.size());
            }

            for (size_t i = 1; i < samples.size(); ++i)
            {
                if (simulationInformation)
                    offset = (*simulationInformation
                                   ->compartmentOffsets)[sectionId] +
                             float(i) * segmentStep;
                else
                    offset = 0;

                Vector4f sample = samples[i];
                Vector3f position(sample.x(), sample.y(), sample.z());
                position += translation;
                _writeToVolume(volume, volumeSize, position, values[offset]);
                previousSample = sample;

                /*
                if (position != target && radius > 0.f && previousRadius > 0.f)
                {
                    if (radius == previousRadius)
                        cylinders[material].push_back(CylinderPtr(
                            new Cylinder(material, position, target, radius,
                                         distance, offset)));
                    else
                        cones[material].push_back(ConePtr(
                            new Cone(material, position, target, radius,
                                     previousRadius, distance, offset)));
                    bounds.merge(target);
                }
                */
            }
            ++sectionId;
        }
    }
    catch (const std::runtime_error &e)
    {
        BRAYNS_ERROR << e.what() << std::endl;
        return false;
    }
    return true;
}

bool MorphologyLoader::_createSimulationVolumeFromCircuit(
    const servus::URI &circuitConfig, const std::string &target,
    const std::string &report, const size_t frame, const floats &values)
{
    const std::string &filename = circuitConfig.getPath();
    const brion::BlueConfig bc(filename);
    const brain::Circuit circuit(bc);
    const brain::GIDSet &gids =
        (target.empty() ? circuit.getGIDs() : circuit.getGIDs(target));
    if (gids.empty())
    {
        BRAYNS_ERROR << "Circuit does not contain any cells" << std::endl;
        return false;
    }

    const Matrix4fs &transforms = circuit.getTransforms(gids);

    const brain::URIs &uris = circuit.getMorphologyURIs(gids);

    // Load simulation information from compartment reports
    const brion::CompartmentReport compartmentReport(
        brion::URI(bc.getReportSource(report).getPath()), brion::MODE_READ,
        gids);

    const brion::CompartmentCounts &compartmentCounts =
        compartmentReport.getCompartmentCounts();

    const brion::SectionOffsets &compartmentOffsets =
        compartmentReport.getOffsets();

    brain::URIs cr_uris;
    const brain::GIDSet &cr_gids = compartmentReport.getGIDs();

    BRAYNS_INFO << "Loading " << cr_gids.size() << " simulated cells"
                << std::endl;
    for (const auto cr_gid : cr_gids)
    {
        auto it = std::find(gids.begin(), gids.end(), cr_gid);
        auto index = std::distance(gids.begin(), it);
        cr_uris.push_back(uris[index]);
    }

    const Vector3ui volumeSize(512, 512, 512);
    floats volume;
    for (size_t i = 0; i < cr_uris.size(); ++i)
    {
        volume.resize(volumeSize.x() * volumeSize.y() * volumeSize.z(), 0.f);
        const SimulationInformation simulationInformation = {
            &compartmentCounts[i], &compartmentOffsets[i]};
        _importSimulationFromMorphology(volume, volumeSize, cr_uris[i],
                                        transforms[i], &simulationInformation,
                                        values);
    }

    // Find range
    float minValue = std::numeric_limits<float>::max();
    float maxValue = -std::numeric_limits<float>::max();
    for (const auto value : volume)
    {
        minValue = std::min(minValue, value);
        maxValue = std::max(maxValue, value);
    }
    const float range = maxValue - minValue;

    // Normalize
    uint8_ts normalizedVolume;
    const float a = 256.f / range;
    for (const auto value : volume)
        normalizedVolume.push_back(minValue + value * a);

    std::stringstream s;
    s << "/home/favreau/medias/volumes/mini50/mini50-" << frame << ".raw";
    std::ofstream volumeFile;
    volumeFile.open(s.str(), std::ios::out | std::ios::binary);
    if (volumeFile.good())
    {
        BRAYNS_INFO << "Create volume: " << s.str() << std::endl;
        for (const auto value : normalizedVolume)
            volumeFile.write((char *)&value, sizeof(char));

        volumeFile.close();
    }
    return true;
}

#else

bool MorphologyLoader::importMorphology(const servus::URI &, const int, Scene &)
{
    BRAYNS_ERROR << "Brion is required to load morphologies" << std::endl;
    return false;
}

bool MorphologyLoader::importCircuit(const servus::URI &, const std::string &,
                                     Scene &)
{
    BRAYNS_ERROR << "Brion is required to load circuits" << std::endl;
    return false;
}

bool MorphologyLoader::importCircuit(const servus::URI &, const std::string &,
                                     const std::string &, Scene &)
{
    BRAYNS_ERROR << "Brion is required to load circuits" << std::endl;
    return false;
}

bool MorphologyLoader::importSimulationData(const servus::URI &,
                                            const std::string &,
                                            const std::string &, Scene &)
{
    BRAYNS_ERROR << "Brion is required to load circuits" << std::endl;
    return false;
}

#endif

size_t MorphologyLoader::_getMaterialFromSectionType(
    const size_t morphologyIndex, const size_t sectionType)
{
    size_t material;
    switch (_geometryParameters.getColorScheme())
    {
    case ColorScheme::neuron_by_id:
        material = morphologyIndex % (NB_MAX_MATERIALS - NB_SYSTEM_MATERIALS);
        break;
    case ColorScheme::neuron_by_segment_type:
        material = sectionType % (NB_MAX_MATERIALS - NB_SYSTEM_MATERIALS);
        break;
    default:
        material = 0;
    }
    return material;
}
}
