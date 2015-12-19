/*
 * This file is part of the Colobot: Gold Edition source code
 * Copyright (C) 2001-2015, Daniel Roux, EPSITEC SA & TerranovaTeam
 * http://epsitec.ch; http://colobot.info; http://github.com/colobot
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see http://gnu.org/licenses
 */

// Modules inlcude
#include "CBotClassInst.h"

#include "CBot/CBotInstr/CBotInstrUtils.h"

#include "CBot/CBotInstr/CBotLeftExprVar.h"
#include "CBot/CBotInstr/CBotTwoOpExpr.h"
#include "CBot/CBotInstr/CBotInstArray.h"

#include "CBot/CBotStack.h"
#include "CBot/CBotCStack.h"
#include "CBot/CBotClass.h"

#include "CBot/CBotVar/CBotVarPointer.h"
#include "CBot/CBotVar/CBotVarClass.h"

// Local include

// Global include


////////////////////////////////////////////////////////////////////////////////
CBotClassInst::CBotClassInst()
{
    m_next          = nullptr;
    m_var           = nullptr;
    m_Parameters    = nullptr;
    m_expr          = nullptr;
    m_hasParams     = false;
    m_nMethodeIdent = 0;
    name = "CBotClassInst";
}

////////////////////////////////////////////////////////////////////////////////
CBotClassInst::~CBotClassInst()
{
    delete m_var;
}

////////////////////////////////////////////////////////////////////////////////
CBotInstr* CBotClassInst::Compile(CBotToken* &p, CBotCStack* pStack, CBotClass* pClass)
{
    // seeks the corresponding classes
    if ( pClass == nullptr )
    {
        pStack->SetStartError(p->GetStart());
        pClass = CBotClass::Find(p);
        if ( pClass == nullptr )
        {
            // not found? is bizare
            pStack->SetError(TX_NOCLASS, p);
            return nullptr;
        }
        p = p->GetNext();
    }

    bool        bIntrinsic = pClass->IsIntrinsic();
    CBotTypResult type = CBotTypResult( bIntrinsic ? CBotTypIntrinsic : CBotTypPointer, pClass );
    CBotClassInst*  inst = static_cast<CBotClassInst*>(CompileArray(p, pStack, type));
    if ( inst != nullptr || !pStack->IsOk() ) return inst;

    CBotCStack* pStk = pStack->TokenStack();

    inst = new CBotClassInst();
    /// TODO Need to be revised and fixed after adding unit tests
    CBotToken token(pClass->GetName(), CBotString(), p->GetStart(), p->GetEnd());
    inst->SetToken(&token);
    CBotToken*  vartoken = p;

    if ( nullptr != (inst->m_var = CBotLeftExprVar::Compile( p, pStk )) )
    {
        (static_cast<CBotLeftExprVar*>(inst->m_var))->m_typevar = type;
        if (pStk->CheckVarLocal(vartoken))                  // redefinition of the variable
        {
            pStk->SetStartError(vartoken->GetStart());
            pStk->SetError(TX_REDEFVAR, vartoken->GetEnd());
            goto error;
        }

        if (IsOfType(p,  ID_OPBRK))                         // with any clues?
        {
            delete inst;                                    // is not type CBotInt
            p = vartoken;                                   // returns to the variable name

            // compiles declaration an array

            inst = static_cast<CBotClassInst*>(CBotInstArray::Compile( p, pStk, type ));

            if (!pStk->IsOk() )
            {
                pStk->SetError(TX_CLBRK, p->GetStart());
                goto error;
            }
            goto suite;         // no assignment, variable already created
        }


        CBotVar*    var;
        var = CBotVar::Create(vartoken->GetString(), type); // creates the instance
//      var->SetClass(pClass);
        var->SetUniqNum(
            (static_cast<CBotLeftExprVar*>(inst->m_var))->m_nIdent = CBotVar::NextUniqNum());
                                                            // its attribute a unique number
        pStack->AddVar(var);                                // placed on the stack

        // look if there are parameters
        inst->m_hasParams = (p->GetType() == ID_OPENPAR);

        CBotVar*    ppVars[1000];
        inst->m_Parameters = CompileParams(p, pStk, ppVars);
        if ( !pStk->IsOk() ) goto error;

        // if there are parameters, is the equivalent to the stament "new"
        // CPoint A ( 0, 0 ) is equivalent to
        // CPoint A = new CPoint( 0, 0 )

//      if ( nullptr != inst->m_Parameters )
        if ( inst->m_hasParams )
        {
            // the constructor is there?
//          CBotString  noname;
            CBotTypResult r = pClass->CompileMethode(pClass->GetName(), var, ppVars, pStk, inst->m_nMethodeIdent);
            delete pStk->TokenStack();                          // releases the supplement stack
            int typ = r.GetType();

            if (typ == TX_UNDEFCALL)
            {
                // si le constructeur n'existe pas
                if (inst->m_Parameters != nullptr)                 // with parameters
                {
                    pStk->SetError(TX_NOCONST, vartoken);
                    goto error;
                }
                typ = 0;
            }

            if (typ>20)
            {
                pStk->SetError(typ, vartoken->GetEnd());
                goto error;
            }

        }

        if (IsOfType(p,  ID_ASS))                           // with a assignment?
        {
            if (inst->m_hasParams)
            {
                pStk->SetError(TX_ENDOF, p->GetStart());
                goto error;
            }

            if ( nullptr == ( inst->m_expr = CBotTwoOpExpr::Compile( p, pStk )) )
            {
                goto error;
            }
            CBotClass* result = pStk->GetClass();
            if ( !pStk->GetTypResult(1).Eq(CBotTypNullPointer) &&
               ( !pStk->GetTypResult(1).Eq(CBotTypPointer) ||
                 ( result != nullptr && !pClass->IsChildOf(result) )))     // type compatible ?
            {
                pStk->SetError(TX_BADTYPE, p->GetStart());
                goto error;
            }
//          if ( !bIntrinsic ) var->SetPointer(pStk->GetVar()->GetPointer());
            if ( !bIntrinsic )
            {
                // does not use the result on the stack, to impose the class
                CBotVar* pvar = CBotVar::Create("", pClass);
                var->SetPointer( pvar );                    // variable already declared instance pointer
                delete pvar;                                // removes the second pointer
            }
            var->SetInit(CBotVar::InitType::DEF);                         // marks the pointer as init
        }
        else if (inst->m_hasParams)
        {
            // creates the object on the "job" (\TODO "tas")
            // with a pointer to the object
            if ( !bIntrinsic )
            {
                CBotVar* pvar = CBotVar::Create("", pClass);
                var->SetPointer( pvar );                    // variable already declared instance pointer
                delete pvar;                                // removes the second pointer
            }
            var->SetInit(CBotVar::InitType::IS_POINTER);                            // marks the pointer as init
        }
suite:
        if (IsOfType(p,  ID_COMMA))                         // several chained definitions
        {
            if ( nullptr != ( inst->m_next = CBotClassInst::Compile(p, pStk, pClass) ))    // compiles the following
            {
                return pStack->Return(inst, pStk);
            }
        }

        if (IsOfType(p,  ID_SEP))                           // complete instruction
        {
            return pStack->Return(inst, pStk);
        }

        pStk->SetError(TX_ENDOF, p->GetStart());
    }

error:
    delete inst;
    return pStack->Return(nullptr, pStk);
}

////////////////////////////////////////////////////////////////////////////////
bool CBotClassInst::Execute(CBotStack* &pj)
{
    CBotVar*    pThis = nullptr;

    CBotStack*  pile = pj->AddStack(this);//essential for SetState()
//  if ( pile == EOX ) return true;

    CBotToken*  pt = &m_token;
    CBotClass*  pClass = CBotClass::Find(pt);

    bool bIntrincic = pClass->IsIntrinsic();

    // creates the variable of type pointer to the object

    if ( pile->GetState()==0)
    {
        CBotString  name = m_var->m_token.GetString();
        if ( bIntrincic )
        {
            pThis = CBotVar::Create(name, CBotTypResult( CBotTypIntrinsic, pClass ));
        }
        else
        {
            pThis = CBotVar::Create(name, CBotTypResult( CBotTypPointer, pClass ));
        }

        pThis->SetUniqNum((static_cast<CBotLeftExprVar*>(m_var))->m_nIdent); // its attribute as unique number
        pile->AddVar(pThis);                                    // place on the stack
        pile->IncState();
    }

    if ( pThis == nullptr ) pThis = pile->FindVar((static_cast<CBotLeftExprVar*>(m_var))->m_nIdent);

    if ( pile->GetState()<3)
    {
        // ss there an assignment or parameters (contructor)

//      CBotVarClass* pInstance = nullptr;

        if ( m_expr != nullptr )
        {
            // evaluates the expression for the assignment
            if (!m_expr->Execute(pile)) return false;

            if ( bIntrincic )
            {
                CBotVar*    pv = pile->GetVar();
                if ( pv == nullptr || pv->GetPointer() == nullptr )
                {
                    pile->SetError(TX_NULLPT, &m_token);
                    return pj->Return(pile);
                }
                pThis->Copy(pile->GetVar(), false);
            }
            else
            {
                CBotVarClass* pInstance;
                pInstance = (static_cast<CBotVarPointer*>(pile->GetVar()))->GetPointer();    // value for the assignment
                pThis->SetPointer(pInstance);
            }
            pThis->SetInit(CBotVar::InitType::DEF);
        }

        else if ( m_hasParams )
        {
            // evaluates the constructor of an instance

            if ( !bIntrincic && pile->GetState() == 1)
            {
                CBotToken*  pt = &m_token;
                CBotClass* pClass = CBotClass::Find(pt);

                // creates an instance of the requested class

                CBotVarClass* pInstance;
                pInstance = static_cast<CBotVarClass*>(CBotVar::Create("", pClass));
                pThis->SetPointer(pInstance);
                delete pInstance;

                pile->IncState();
            }

            CBotVar*    ppVars[1000];
            CBotStack*  pile2 = pile;

            int     i = 0;

            CBotInstr*  p = m_Parameters;
            // evaluates the parameters
            // and places the values ​​on the stack
            // to (can) be interrupted (broken) at any time

            if ( p != nullptr) while ( true )
            {
                pile2 = pile2->AddStack();                      // place on the stack for the results
                if ( pile2->GetState() == 0 )
                {
                    if (!p->Execute(pile2)) return false;       // interrupted here?
                    pile2->SetState(1);
                }
                ppVars[i++] = pile2->GetVar();
                p = p->GetNext();
                if ( p == nullptr) break;
            }
            ppVars[i] = nullptr;

            // creates a variable for the result
            CBotVar*    pResult = nullptr;     // constructor still void

            if ( !pClass->ExecuteMethode(m_nMethodeIdent, pClass->GetName(),
                                         pThis, ppVars,
                                         pResult, pile2, GetToken())) return false; // interrupt

            pThis->SetInit(CBotVar::InitType::DEF);
            pThis->ConstructorSet();        // indicates that the constructor has been called
            pile->Return(pile2);                                // releases a piece of stack

//          pInstance = pThis->GetPointer();

        }

//      if ( !bIntrincic ) pThis->SetPointer(pInstance);        // a pointer to the instance

        pile->SetState(3);                                  // finished this part
    }

    if ( pile->IfStep() ) return false;

    if ( m_next2b != nullptr &&
        !m_next2b->Execute(pile)) return false;             // other (s) definition (s)

    return pj->Return( pile );                              // transmits below (further)
}

////////////////////////////////////////////////////////////////////////////////
void CBotClassInst::RestoreState(CBotStack* &pj, bool bMain)
{
    CBotVar*    pThis = nullptr;

    CBotStack*  pile = pj;
    if ( bMain ) pile = pj->RestoreStack(this);
    if ( pile == nullptr ) return;

    // creates the variable of type pointer to the object
    {
        CBotString  name = m_var->m_token.GetString();
        pThis = pile->FindVar(name);
        pThis->SetUniqNum((static_cast<CBotLeftExprVar*>(m_var))->m_nIdent); // its attribute a unique number
    }

    CBotToken*  pt = &m_token;
    CBotClass*  pClass = CBotClass::Find(pt);
    bool bIntrincic = pClass->IsIntrinsic();

    if ( bMain && pile->GetState()<3)
    {
        // is there an assignment or parameters (constructor)

//      CBotVarClass* pInstance = nullptr;

        if ( m_expr != nullptr )
        {
            // evaluates the expression for the assignment
            m_expr->RestoreState(pile, bMain);
            return;
        }

        else if ( m_hasParams )
        {
            // evaluates the constructor of an instance

            if ( !bIntrincic && pile->GetState() == 1)
            {
                return;
            }

            CBotVar*    ppVars[1000];
            CBotStack*  pile2 = pile;

            int     i = 0;

            CBotInstr*  p = m_Parameters;
            // evaluates the parameters
            // and the values an the stack
            // for the ability to be interrupted at any time (\TODO pour pouvoir être interrompu n'importe quand)

            if ( p != nullptr) while ( true )
            {
                pile2 = pile2->RestoreStack();                      // place on the stack for the results
                if ( pile2 == nullptr ) return;

                if ( pile2->GetState() == 0 )
                {
                    p->RestoreState(pile2, bMain);      // interrupted here?
                    return;
                }
                ppVars[i++] = pile2->GetVar();
                p = p->GetNext();
                if ( p == nullptr) break;
            }
            ppVars[i] = nullptr;

            // creates a variable for the result
//            CBotVar*    pResult = nullptr;     // constructor still void

            pClass->RestoreMethode(m_nMethodeIdent, pClass->GetName(), pThis, ppVars, pile2);
            return;
        }
    }

    if ( m_next2b != nullptr )
         m_next2b->RestoreState(pile, bMain);                   // other(s) definition(s)
}