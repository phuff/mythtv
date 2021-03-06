//////////////////////////////////////////////////////////////////////////////
// Program Name: htmlserver.cpp
// Created     : Mar. 9, 2011
//
// Purpose     : Http server extension to serve up static html content
//                                                                            
// Copyright (c) 2011 David Blain <dblain@mythtv.org>
//                                          
// Licensed under the GPL v2 or later, see COPYING for details                    
//
//////////////////////////////////////////////////////////////////////////////

#include "mythlogging.h"
#include "htmlserver.h"
#include "storagegroup.h"
#include "httprequest.h"

#include "serviceHosts/rttiServiceHost.h"

#include <QFileInfo>
#include <QDir>
#include <QTextStream>

/////////////////////////////////////////////////////////////////////////////
//
/////////////////////////////////////////////////////////////////////////////

HtmlServerExtension::HtmlServerExtension( const QString &sSharePath,
                                          const QString &sApplicationPrefix)
  : HttpServerExtension( "Html" , sSharePath),
    m_IndexFilename(sApplicationPrefix + "index")
{
    LOG(VB_HTTP, LOG_INFO, QString("HtmlServerExtension() - SharePath = %1")
            .arg(m_sSharePath));
    m_Scripting.SetResourceRootPath( m_sSharePath );
    
    // ----------------------------------------------------------------------
    // Register Rtti with QtScript Engine.
    // Rtti exposes internal enums complete with translations for use in scripts
    // ----------------------------------------------------------------------

    QScriptEngine *pEngine = ScriptEngine();
    pEngine->globalObject().setProperty("Rtti",
         pEngine->scriptValueFromQMetaObject< ScriptableRtti >() );
}

/////////////////////////////////////////////////////////////////////////////
//
/////////////////////////////////////////////////////////////////////////////

HtmlServerExtension::~HtmlServerExtension( )
{
}

/////////////////////////////////////////////////////////////////////////////
//
/////////////////////////////////////////////////////////////////////////////

bool HtmlServerExtension::ProcessRequest( HTTPRequest *pRequest )
{
    if (pRequest)
    {
        if ( pRequest->m_sBaseUrl.startsWith("/") == false)
            return( false );

        bool      bStorageGroupFile = false;
        QFileInfo oInfo( m_sSharePath + pRequest->m_sResourceUrl );

        if (oInfo.isDir())
        {
            QString sIndexFileName = oInfo.filePath() + m_IndexFilename + ".qsp";

            if (QFile::exists( sIndexFileName ))
                oInfo.setFile( sIndexFileName );
            else 
                oInfo.setFile( oInfo.filePath() + m_IndexFilename + ".html" );
        }

        if (pRequest->m_sResourceUrl.startsWith("/StorageGroup/"))
        {
            StorageGroup oGroup(pRequest->m_sResourceUrl.section('/', 2, 2));
            QString      sFile =
                oGroup.FindFile(pRequest->m_sResourceUrl.section('/', 3));
            if (!sFile.isEmpty())
            {
                oInfo.setFile(sFile);
                bStorageGroupFile = true;
            }
        }

        if (bStorageGroupFile || oInfo.exists() == true )
        {
            QString sResName = oInfo.canonicalFilePath();

            // --------------------------------------------------------------
            // Checking for url's that contain ../ or similar.
            // --------------------------------------------------------------

            if (( bStorageGroupFile ) ||
                (sResName.startsWith( m_sSharePath, Qt::CaseInsensitive )))
            {
                if (oInfo.exists())
                {
                    if (oInfo.isSymLink())
                        sResName = oInfo.symLinkTarget();

                    // ------------------------------------------------------
                    // Is this a Qt Server Page (File contains script)...
                    // ------------------------------------------------------

                    QString sSuffix = oInfo.suffix().toLower();

                    QString sMimeType = HTTPRequest::GetMimeType(sSuffix);

                    if (sMimeType == "text/html")
                        pRequest->m_eResponseType = ResponseTypeHTML;
                    else if (sMimeType == "text/xml")
                        pRequest->m_eResponseType = ResponseTypeXML;
                    else if (sMimeType == "application/javascript")
                        pRequest->m_eResponseType = ResponseTypeJS;
                    else if (sMimeType == "text/css")
                        pRequest->m_eResponseType = ResponseTypeCSS;
                    else if (sMimeType == "text/plain")
                        pRequest->m_eResponseType = ResponseTypeText;
                    else if (sMimeType == "image/svg+xml" &&
                              sSuffix != "svgz") // svgz are pre-compressed
                        pRequest->m_eResponseType = ResponseTypeSVG;

                    if ((sSuffix == "qsp") ||
                        (sSuffix == "qxml") ||
                        (sSuffix == "qjs" )) 
                    {
                        QTextStream stream( &pRequest->m_response );
                        
                        m_Scripting.EvaluatePage( &stream, sResName, pRequest);

                        return true;

                    }

                    // ----------------------------------------------------------------------
                    // Force IE into 'standards' mode
                    // ----------------------------------------------------------------------
                    pRequest->SetResponseHeader( "X-UA-Compatible" , "IE=Edge");

                    // ---------------------------------------------------------
                    // SECURITY: Set Content Security Policy (no external content allowed)
                    // This is an important safeguard. Third party content
                    // should never be permitted. It compromises security,
                    // privacy and violates the key principal that the WebFrontend
                    // should work on an isolated network with no internet
                    // access. Keep all content hosted locally!
                    // ---------------------------------------------------------
                    pRequest->SetResponseHeader("Content-Security-Policy",
                                                // For now the following are disabled as we use xhr to trigger playback on frontends
                                                // if we switch to triggering that through an internal request then these would be better
                                                // enabled
                                                //"default-src 'self'; "
                                                //"connect-src 'self; "
                                                "script-src 'self' 'unsafe-inline' 'unsafe-eval'; " // FIXME: unsafe-inline should be phased out, replaced by nonce-{csp_nonce}
                                                "style-src 'self' 'unsafe-inline'; "
                                                "frame-src 'none'; "
                                                "object-src 'self'; " // TODO: When we no longer require flash for some browsers, change this to 'none'
                                                "media-src 'self'; "
                                                "font-src 'self'; "
                                                "image-src 'self'; "
                                                "reflected-xss filter;");

                    // ------------------------------------------------------
                    // Return the file.
                    // ------------------------------------------------------

                    pRequest->FormatFileResponse( sResName );

                    return true;
                }
            }
        }

        // force return as a 404...
        pRequest->FormatFileResponse( "" );
    }

    return( true );
}

