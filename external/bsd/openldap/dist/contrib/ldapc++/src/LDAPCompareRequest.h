/*	$NetBSD: LDAPCompareRequest.h,v 1.4 2025/09/05 21:16:14 christos Exp $	*/

// $OpenLDAP$
/*
 * Copyright 2000-2024 The OpenLDAP Foundation, All Rights Reserved.
 * COPYING RESTRICTIONS APPLY, see COPYRIGHT file
 */

#ifndef LDAP_COMPARE_REQUEST_H
#define LDAP_COMPARE_REQUEST_H

#include <LDAPRequest.h>

class LDAPMessageQueue;

class LDAPCompareRequest : public LDAPRequest {
    public :
        LDAPCompareRequest(const LDAPCompareRequest& req);
        LDAPCompareRequest(const std::string& dn, const LDAPAttribute& attr, 
                LDAPAsynConnection *connect, const LDAPConstraints *cons,
                bool isReferral=false, const LDAPRequest* parent=0);
        virtual ~LDAPCompareRequest();
        virtual LDAPMessageQueue* sendRequest();
        virtual LDAPRequest* followReferral(LDAPMsg* urls);
    
    private :
        std::string m_dn;
        LDAPAttribute m_attr;
        
};
#endif //LDAP_COMPARE_REQUEST_H


