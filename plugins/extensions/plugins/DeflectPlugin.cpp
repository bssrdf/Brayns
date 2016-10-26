/* Copyright (c) 2015-2016, EPFL/Blue Brain Project
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

#include "DeflectPlugin.h"

#include <brayns/Brayns.h>
#include <plugins/engines/common/Engine.h>
#include <brayns/common/scene/Scene.h>
#include <brayns/common/renderer/FrameBuffer.h>
#include <brayns/common/renderer/Renderer.h>
#include <brayns/common/camera/Camera.h>
#include <brayns/common/input/KeyboardHandler.h>
#include <brayns/parameters/ApplicationParameters.h>

#ifdef BRAYNS_USE_ZEROEQ
#  include "ZeroEQPlugin.h"
#endif

namespace brayns
{

#ifdef BRAYNS_USE_ZEROEQ
    DeflectPlugin::DeflectPlugin( Brayns& brayns, ZeroEQPlugin& zeroeq )
#else
    DeflectPlugin::DeflectPlugin( Brayns& brayns )
#endif
    : ExtensionPlugin( brayns )
    , _theta( 0.f )
    , _phi( 0.f )
    , _previousTouchPosition( 0.5f, 0.5f, -1.f )
    , _stream( nullptr )
    , _pressed( false )
{
    brayns.getKeyboardHandler().registerKeyboardShortcut(
        '*', "Enable/Disable Deflect streaming",
                [&] { _params.setEnabled( !_params.getEnabled( )); });

#ifdef BRAYNS_USE_ZEROEQ
    zeroeq.handleObject( _params );
#endif
}

void DeflectPlugin::run()
{
    if( _stream )
    {
        const bool changed = _stream->getId() != _params.getIdString() ||
                             _stream->getHost() != _params.getHostString();
        if( changed )
            _stream.reset();
    }

    const bool deflectEnabled = _params.getEnabled();
    if( _stream && _stream->isConnected() && !deflectEnabled )
    {
        BRAYNS_INFO << "Closing Deflect stream" << std::endl;
        _stream.reset();
    }

    if( deflectEnabled && !_stream )
        _initializeDeflect();

    if( deflectEnabled && _stream && _stream->isConnected() )
    {
        _sendDeflectFrame();
        _handleDeflectEvents();
    }
}

void DeflectPlugin::_initializeDeflect()
{
    try
    {
        _stream.reset( new deflect::Stream( _params.getIdString(),
                                            _params.getHostString(),
                                            _params.getPort( )));

        if( _stream->isConnected( ))
            BRAYNS_INFO << "Deflect successfully connected to Tide on host "
                        << _stream->getHost() << std::endl;
        else
            BRAYNS_ERROR << "Deflect failed to connect to Tide on host "
                         << _stream->getHost() << std::endl;

        if( !_stream->registerForEvents( ))
            BRAYNS_ERROR << "Deflect failed to register for events!" << std::endl;

        _params.setId( _stream->getId( ));
        _params.setHost( _stream->getHost( ));
    }
    catch( std::runtime_error& ex )
    {
        BRAYNS_ERROR << "Deflect failed to initialize. " << ex.what() << std::endl;
        _params.setEnabled( false );
        return;
    }
}

void DeflectPlugin::_sendDeflectFrame()
{
    FrameBuffer& frameBuffer = _brayns.getFrameBuffer();
    const Vector2i& frameSize = frameBuffer.getSize();
    void* data = frameBuffer.getColorBuffer();
    if( data )
        _send( frameSize, (unsigned long*)data, true);
}

void DeflectPlugin::_handleDeflectEvents()
{
    bool hasEvents = false;

    FrameBuffer& frameBuffer = _brayns.getFrameBuffer();
    while( _stream->hasEvent() )
    {
        hasEvents = true;
        const deflect::Event& event = _stream->getEvent();

        switch( event.type )
        {
        case deflect::Event::EVT_PRESS:
        {
            _previousTouchPosition.x() = event.mouseX;
            _previousTouchPosition.y() = event.mouseY;
            _pressed = true;
            break;
        }
        case deflect::Event::EVT_RELEASE:
        {
            _pressed = false;
            break;
        }
        case deflect::Event::EVT_MOVE:
        case deflect::Event::EVT_WHEEL:
        {
            if( _pressed )
            {
                const float du = _previousTouchPosition.x() - event.mouseX;
                const float dv = _previousTouchPosition.y() - event.mouseY;

                _theta -= frameBuffer.getSize().x() / 100.f * std::asin( du );
                _phi += frameBuffer.getSize().y() / 100.f * std::asin( dv );

                _previousTouchPosition.x() = event.mouseX;
                _previousTouchPosition.y() = event.mouseY;
                _previousTouchPosition.z() = std::min(0.f, _previousTouchPosition.z());
            }
            else
                _previousTouchPosition.z() += event.dy;

            Scene& scene = _brayns.getScene();
            const Vector3f& center = scene.getWorldBounds().getCenter();
            const Vector3f& size = scene.getWorldBounds().getSize();
            const Vector3f cameraPosition = center + size * Vector3f(
                _previousTouchPosition.z( ) * std::cos( _phi ) * std::cos( _theta ),
                _previousTouchPosition.z( ) * std::sin( _phi ) * std::cos( _theta ),
                _previousTouchPosition.z( ) * std::sin( _theta ));

            _brayns.getCamera().setPosition( cameraPosition );
            _brayns.getCamera().setTarget( center );
            break;
        }
        case deflect::Event::EVT_KEY_PRESS:
        {
            _brayns.getEngine().getKeyboardHandler()->handleKeyboardShortcut( event.text[0] );
            break;
        }
        case deflect::Event::EVT_VIEW_SIZE_CHANGED:
        {
            _brayns.reshape( Vector2ui( event.dx, event.dy ) );
            break;
        }
        case deflect::Event::EVT_NONE:
        case deflect::Event::EVT_CLICK:
        default:
            break;
        }
    }
    if( hasEvents )
    {
        _brayns.getFrameBuffer().clear();
        _brayns.getRenderer().commit();
    }
}

void DeflectPlugin::_send(
    const Vector2i& windowSize,
    unsigned long* imageData,
    const bool swapXAxis )
{
    deflect::ImageWrapper deflectImage( imageData, windowSize.x(), windowSize.y(), deflect::RGBA );

    deflectImage.compressionQuality = _params.getQuality();
    deflectImage.compressionPolicy =
        _params.getCompression() ?
        deflect::COMPRESSION_ON : deflect::COMPRESSION_OFF;
    if( swapXAxis )
        deflect::ImageWrapper::swapYAxis( (void*)imageData, windowSize.x(), windowSize.y(), 4);

    // TODO: Big Memory Leak on every send. FIX NEEDED!!!!!
    const bool success = _stream->send( deflectImage ) && _stream->finishFrame();
    if(!success)
    {
        if( !_stream->isConnected() )
            BRAYNS_ERROR << "Stream closed, exiting." << std::endl;
        else
            BRAYNS_ERROR << "failure in deflectStreamSend()" << std::endl;
    }
}

bool DeflectPlugin::_handleTouchEvents( HandledEvents& handledEvents )
{
    if(!_stream || !_stream->isRegisteredForEvents())
        return false;

    /* increment rotation angle according to interaction, or by a constant rate
     * if interaction is not enabled. Note that mouse position is in normalized
     * window coordinates: (0,0) to (1,1)
     * Note: there is a risk of missing events since we only process the
     * latest state available. For more advanced applications, event
     * processing should be done in a separate thread.
     */
    while(_stream->hasEvent())
    {
        const deflect::Event& event = _stream->getEvent();
        if(event.type == deflect::Event::EVT_CLOSE)
        {
            BRAYNS_INFO << "Received close..." << std::endl;
            handledEvents.closeApplication = true;
        }

        handledEvents.pressed = (event.type == deflect::Event::EVT_PRESS);

        if (event.type == deflect::Event::EVT_WHEEL)
            handledEvents.wheelDelta = Vector2f(event.dx, event.dy);

        handledEvents.position = Vector2f(event.mouseX, event.mouseY);
    }
    return true;
}

}
