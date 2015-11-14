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
#include "CBotWhile.h"
#include "CBotBlock.h"
#include "CBotCondition.h"

#include "CBotStack.h"

// Local include

// Global include

////////////////////////////////////////////////////////////////////////////////
CBotWhile::CBotWhile()
{
    m_Condition =
    m_Block     = nullptr;     // nullptr so that delete is not possible further
    name = "CBotWhile";     // debug
}

////////////////////////////////////////////////////////////////////////////////
CBotWhile::~CBotWhile()
{
    delete  m_Condition;    // frees the condition
    delete  m_Block;        // releases the block instruction
}

////////////////////////////////////////////////////////////////////////////////
CBotInstr* CBotWhile::Compile(CBotToken* &p, CBotCStack* pStack)
{
    CBotWhile*  inst = new CBotWhile();         // creates the object
    CBotToken*  pp = p;                         // preserves at the ^ token (starting position)

    if ( IsOfType( p, TokenTypVar ) &&
         IsOfType( p, ID_DOTS ) )
    {
        inst->m_label = pp->GetString();        // records the name of the label
    }

    inst->SetToken(p);
    if (!IsOfType(p, ID_WHILE)) return nullptr;    // should never happen

    CBotCStack* pStk = pStack->TokenStack(pp);  // un petit bout de pile svp
                                                // a bit of battery please (??)

    if ( nullptr != (inst->m_Condition = CBotCondition::Compile( p, pStk )) )
    {
        // the condition exists

        IncLvl(inst->m_label);
        inst->m_Block = CBotBlock::CompileBlkOrInst( p, pStk, true );
        DecLvl();

        if ( pStk->IsOk() )
        {
            // the statement block is ok (it may be empty!

            return pStack->Return(inst, pStk);  // return an object to the application
                                                // makes the object to which the application
        }
    }

    delete inst;                                // error, frees the place
    return pStack->Return(nullptr, pStk);          // no object, the error is on the stack
}

////////////////////////////////////////////////////////////////////////////////
bool CBotWhile::Execute(CBotStack* &pj)
{
    CBotStack* pile = pj->AddStack(this);   // adds an item to the stack
                                            // or find in case of recovery
//  if ( pile == EOX ) return true;

    if ( pile->IfStep() ) return false;

    while( true ) switch( pile->GetState() )    // executes the loop
    {                                           // there are two possible states (depending on recovery)
    case 0:
        // evaluates the condition
        if ( !m_Condition->Execute(pile) ) return false; // interrupted here?

        // the result of the condition is on the stack

        // terminates if an error or if the condition is false
        if ( !pile->IsOk() || pile->GetVal() != true )
        {
            return pj->Return(pile);                    // sends the results and releases the stack
        }

        // the condition is true, pass in the second mode

        if (!pile->SetState(1)) return false;           // ready for further

    case 1:
        // evaluates the associated statement block
        if ( m_Block != nullptr &&
            !m_Block->Execute(pile) )
        {
            if (pile->IfContinue(0, m_label)) continue; // if continued, will return to test
            return pj->BreakReturn(pile, m_label);      // sends the results and releases the stack
        }

        // terminates if there is an error
        if ( !pile->IsOk() )
        {
            return pj->Return(pile);                    // sends the results and releases the stack
        }

        // returns to the test again
        if (!pile->SetState(0, 0)) return false;
        continue;
    }
}

////////////////////////////////////////////////////////////////////////////////
void CBotWhile::RestoreState(CBotStack* &pj, bool bMain)
{
    if ( !bMain ) return;
    CBotStack* pile = pj->RestoreStack(this);   // adds an item to the stack
    if ( pile == nullptr ) return;

    switch( pile->GetState() )
    {                                           // there are two possible states (depending on recovery)
    case 0:
        // evaluates the condition
        m_Condition->RestoreState(pile, bMain);
        return;

    case 1:
        // evaluates the associated statement block
        if ( m_Block != nullptr ) m_Block->RestoreState(pile, bMain);
        return;
    }
}
