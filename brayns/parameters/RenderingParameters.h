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

#ifndef RENDERINGPARAMETERS_H
#define RENDERINGPARAMETERS_H

#include <brayns/common/types.h>

#include "AbstractParameters.h"

namespace brayns
{
class AbstractParameters;

/** Manages rendering parameters
 */
class RenderingParameters : public AbstractParameters
{
public:
    RenderingParameters();

    /** @copydoc AbstractParameters::print */
    void print() final;

    /** Engine*/
    const std::string& getEngine() const { return _engine; }
    void setEngine(const std::string& name) { updateValue(_engine, name); }
    /** OSPRay module */
    const std::string& getModule() const { return _module; }
    /** OSPRay renderer */
    RendererType getRenderer() const { return _renderer; }
    const std::string& getRendererAsString(const RendererType value) const;
    const std::string& getRendererNameAsString(const RendererType value) const;
    void setRenderer(const RendererType renderer)
    {
        updateValue(_renderer, renderer);
    }
    /** OSPRay supported renderers */
    const RendererTypes& getRenderers() const { return _renderers; }
    /** Shadows */
    float getShadows() const { return _shadows; }
    void setShadows(const float value) { updateValue(_shadows, value); }
    /** Softs Shadows generated by randomizing light source position */
    float getSoftShadows() const { return _softShadows; }
    void setSoftShadows(const float value) { updateValue(_softShadows, value); }
    /** Ambient occlusion */
    float getAmbientOcclusionStrength() const
    {
        return _ambientOcclusionStrength;
    }
    void setAmbientOcclusionStrength(const float value)
    {
        updateValue(_ambientOcclusionStrength, value);
    }
    float getAmbientOcclusionDistance() const
    {
        return _ambientOcclusionDistance;
    }
    void setAmbientOcclusionDistance(const float value)
    {
        updateValue(_ambientOcclusionDistance, value);
    }

    /** Shading applied to the geometry
     */
    ShadingType getShading() const { return _shading; }
    const std::string& getShadingAsString(const ShadingType value) const;
    void setShading(const ShadingType value) { updateValue(_shading, value); }
    /** Number of samples per pixel */
    int getSamplesPerPixel() const { return _spp; }
    void setSamplesPerPixel(const int value) { updateValue(_spp, value); }
    /** Enables photon emission according to the radiance value of the
     * material */
    bool getLightEmittingMaterials() const { return _lightEmittingMaterials; }
    void setLightEmittingMaterials(const bool value)
    {
        updateValue(_lightEmittingMaterials, value);
    }

    bool getDynamicLoadBalancer() const { return _dynamicLoadBalancer; }
    void setDynamicLoadBalancer(const bool value)
    {
        updateValue(_dynamicLoadBalancer, value);
    }

    const Vector3f& getBackgroundColor() const { return _backgroundColor; }
    void setBackgroundColor(const Vector3f& value)
    {
        updateValue(_backgroundColor, value);
    }
    /**
       Defines the maximum distance between intersection and surrounding
       geometry for touch detection rendering
    */
    float getDetectionDistance() const { return _detectionDistance; }
    void setDetectionDistance(const float value)
    {
        updateValue(_detectionDistance, value);
    }
    /**
       Defines if touch detection applies if the material of the surrounding
       geometry is different from the one of the intersection
    */
    bool getDetectionOnDifferentMaterial() const
    {
        return _detectionOnDifferentMaterial;
    }
    void setDetectionOnDifferentMaterial(const bool value)
    {
        updateValue(_detectionOnDifferentMaterial, value);
    }

    /**
       Near color used by touch detection renderer
    */
    const Vector3f& getDetectionNearColor() const
    {
        return _detectionNearColor;
    }
    void setDetectionNearColor(const Vector3f& value)
    {
        updateValue(_detectionNearColor, value);
    }

    /**
       Far color used by touch detection renderer
    */
    const Vector3f& getDetectionFarColor() const { return _detectionFarColor; }
    void setDetectionFarColor(const Vector3f& value)
    {
        updateValue(_detectionFarColor, value);
    }

    /**
        Raytracers have to deal with the finite precision of computer
        calculations. Since the origin of the reflected ray lies on the surface
        of the object, there will be an intersection point at zero distance.
        Since we do not want that, all intersection distances less than the
        epsilon value are ignored.
     */
    float getEpsilon() const { return _epsilon; }
    void setEpsilon(const float epsilon) { updateValue(_epsilon, epsilon); }
    /**
       Camera type
    */
    CameraType getCameraType() const { return _cameraType; }
    const std::string& getCameraTypeAsString(const CameraType value) const;

    /**
       Epsilon. All intersection distances less than the epsilon value are
       ignored by the raytracer.
    */
    void setEpsilon(const CameraType cameraType)
    {
        updateValue(_cameraType, cameraType);
    }
    /**
       Light source follow camera origin
    */
    bool getHeadLight() const { return _headLight; }
    /** If the rendering should be refined by accumulating multiple passes */
    AccumulationType getAccumulationType() const { return _accumulationType; }
    const std::string getAccumulationTypeAsString(const AccumulationType value);

    /**
     * @return the threshold where accumulation stops if the variance error
     * reaches this value.
     */
    float getVarianceThreshold() const { return _varianceThreshold; }
    /**
     * The threshold where accumulation stops if the variance error reaches this
     * value.
     */
    void setVarianceThreshold(const float value)
    {
        updateValue(_varianceThreshold, value);
    }

protected:
    bool _parse(const po::variables_map& vm) final;

    std::string _engine;
    std::string _module;
    RendererType _renderer;
    RendererTypes _renderers;
    float _ambientOcclusionStrength;
    float _ambientOcclusionDistance;
    ShadingType _shading;
    bool _lightEmittingMaterials;
    int _spp;
    AccumulationType _accumulationType{AccumulationType::linear};
    float _shadows;
    float _softShadows;
    Vector3f _backgroundColor;
    float _detectionDistance;
    bool _detectionOnDifferentMaterial;
    Vector3f _detectionNearColor;
    Vector3f _detectionFarColor;
    float _epsilon;
    CameraType _cameraType;
    bool _headLight;
    bool _dynamicLoadBalancer{false};
    float _varianceThreshold{-1.f};
};
}
#endif // RENDERINGPARAMETERS_H
