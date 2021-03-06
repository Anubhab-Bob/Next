#pragma once

#include "expr.h"
#include "hashmap.h"
#include "objects/customdeque.h"
#include "stmt.h"

class Precedence {
	// Ordered in increasing precedence.
  public:
	static const int ASSIGNMENT = 1;
	static const int OR         = 2;
	static const int AND        = 3;
	static const int EQUALITY   = 4;
	static const int COMPARISON = 5;
	static const int SUM        = 6;
	static const int PRODUCT    = 7;
	static const int EXPONENT   = 8;
	static const int PREFIX     = 9;
	static const int POSTFIX    = 10;
	static const int REFERENCE  = 11;
	static const int CALL       = 12;
	static const int PRIMARY    = 13;
};

class Parser;

class PrefixParselet {
  public:
	virtual Expression *parse(Parser *parser, Token t) = 0;
	virtual ~PrefixParselet() {}
};

class NameParselet : public PrefixParselet {
  public:
	Expression *parse(Parser *parser, Token t);
};

class ThisOrSuperParselet : public PrefixParselet {
  public:
	Expression *parse(Parser *parser, Token t);
};

class LiteralParselet : public PrefixParselet {
  public:
	Expression *parse(Parser *parser, Token t);
};

class ArrayLiteralParselet : public PrefixParselet {
  public:
	Expression *parse(Parser *parser, Token t);
};

class HashmapLiteralParselet : public PrefixParselet {
  public:
	Expression *parse(Parser *parser, Token t);
};

class PrefixOperatorParselet : public PrefixParselet {
  private:
	int precedence;

  public:
	PrefixOperatorParselet(int prec) : precedence(prec) {}
	Expression *parse(Parser *parser, Token t);
	int         getPrecedence() { return precedence; }
};

class GroupParselet : public PrefixParselet {
  public:
	Expression *parse(Parser *parser, Token t);
};

class InfixParselet {
  private:
	int precedence;

  public:
	InfixParselet(int prec) : precedence(prec) {}
	virtual Expression *parse(Parser *parser, const Expression2 &left,
	                          Token t) = 0;
	int                 getPrecedence() { return precedence; }
	virtual bool        isAssignment() { return false; }
	virtual ~InfixParselet() {}
};

class BinaryOperatorParselet : public InfixParselet {
  private:
	bool isRight;

  public:
	BinaryOperatorParselet(int precedence, bool isr)
	    : InfixParselet(precedence), isRight(isr) {}
	Expression *parse(Parser *parser, const Expression2 &left, Token t);
};

class PostfixOperatorParselet : public InfixParselet {
  public:
	PostfixOperatorParselet(int precedence) : InfixParselet(precedence) {}
	Expression *parse(Parser *parser, const Expression2 &left, Token t);
	bool        isAssignment() {
        return true; // only ++/--
	}
};

class AssignParselet : public InfixParselet {
  public:
	AssignParselet() : InfixParselet(Precedence::ASSIGNMENT) {}
	Expression *parse(Parser *parser, const Expression2 &left, Token t);
	bool        isAssignment() { return true; }
};

class CallParselet : public InfixParselet {
  public:
	CallParselet() : InfixParselet(Precedence::CALL) {}
	Expression *parse(Parser *parser, const Expression2 &left, Token t);
};

class ReferenceParselet : public InfixParselet {
  public:
	ReferenceParselet() : InfixParselet(Precedence::REFERENCE) {}
	Expression *parse(Parser *parser, const Expression2 &left, Token t);
};

class SubscriptParselet : public InfixParselet {
  public:
	SubscriptParselet() : InfixParselet(Precedence::REFERENCE) {}
	Expression *parse(Parser *parser, const Expression2 &left, Token t);
};

class DeclarationParselet {
  public:
	Statement *parse(Parser *p, Token t) {
		return this->parse(p, t, VIS_DEFAULT);
	}
	virtual Statement *parse(Parser *p, Token t, Visibility vis) = 0;
	virtual ~DeclarationParselet() {}
};

class StatementParselet {
  public:
	virtual Statement *parse(Parser *p, Token t) = 0;
	virtual ~StatementParselet() {}
};

class ImportDeclaration : public DeclarationParselet {
  public:
	Statement *
	parse(Parser *p, Token t,
	      Visibility vis); // vis_priv will throw an exception for import
};

class ClassDeclaration : public DeclarationParselet {
  private:
	Statement *                                    parseClassBody(Parser *p);
	static HashMap<TokenType, StatementParselet *> classBodyParselets;

  public:
	static void registerParselet(TokenType t, StatementParselet *parselet);
	Visibility  memberVisibility;
	Statement * parse(Parser *p, Token t, Visibility vis);
};

class FnDeclaration : public DeclarationParselet {
  public:
	virtual Statement *parse(Parser *p, Token t, Visibility vis);
	static FnBodyStatement *
	                  parseFnBody(Parser *p, Token t, bool isNative = false, int numArgs = -1);
	static Statement *parseFnStatement(Parser *p, Token t, bool ism, bool iss,
	                                   Visibility vis);
};

class VarDeclaration : public DeclarationParselet {
	Statement *parse(Parser *p, Token t, Visibility vis);
};

class IfStatementParselet : public StatementParselet {
  public:
	Statement *parse(Parser *p, Token t);
};

class WhileStatementParselet : public StatementParselet {
  public:
	Statement *parse(Parser *p, Token t);
};

class DoStatementParselet : public StatementParselet {
  public:
	Statement *parse(Parser *p, Token t);
};

class TryStatementParselet : public StatementParselet {
  public:
	Statement *parse(Parser *p, Token t);
};

class ThrowStatementParselet : public StatementParselet {
  public:
	Statement *parse(Parser *p, Token t);
};

class ReturnStatementParselet : public StatementParselet {
  public:
	Statement *parse(Parser *p, Token t);
};

class ForStatementParselet : public StatementParselet {
  public:
	Statement *parse(Parser *p, Token t);
};

class BreakStatementParselet : public StatementParselet {
  public:
	Statement *parse(Parser *p, Token t);
};

// Class Body Statements

class ConstructorDeclaration : public StatementParselet {
  public:
	Statement *parse(Parser *p, Token t);
};

class VisibilityDeclaration : public StatementParselet {
  public:
	Statement *parse(Parser *p, Token t);
};

// it can either be a static block, or a static function,
// or a static variable
class StaticDeclaration : public StatementParselet {
  public:
	Statement *parse(Parser *p, Token t);
};

class MethodDeclaration : public StatementParselet {
  public:
	Statement *parse(Parser *p, Token t);
};

class OpMethodDeclaration : public StatementParselet {
  public:
	Statement *parse(Parser *p, Token t);
};

class MemberDeclaration : public StatementParselet {
  public:
	Statement *       parse(Parser *p, Token t);
	static Statement *parse(Parser *p, Token t, bool isStatic);
};

// Parser

class Parser {
  private:
	HashMap<TokenType, PrefixParselet *>      prefixParselets;
	HashMap<TokenType, InfixParselet *>       infixParselets;
	HashMap<TokenType, DeclarationParselet *> declarationParselets;
	HashMap<TokenType, StatementParselet *>   statementParselets;
	Scanner &                                 scanner;
	CustomDeque<Token>                        tokenCache;

	int getPrecedence();

	Array *declarations; // root of the tree currently being compiled
  public:
	Parser(Scanner &sc);
	Token lookAhead(size_t distance);
	bool  match(TokenType expected);
	Token consume();
	Token consume(TokenType expected, const char *message);
	void  registerParselet(TokenType type, PrefixParselet *p);
	void  registerParselet(TokenType type, InfixParselet *p);
	void  registerParselet(TokenType type, DeclarationParselet *p);
	void  registerParselet(TokenType type, StatementParselet *p);
	// if silent is true, the parser won't trigger an
	// exception if it cannot find a suitable expression
	// to parse. it will bail out and return null
	Expression *parseExpression(Token token, bool silent = false);
	Expression *parseExpression(bool silent = false);
	Expression *parseExpression(int precedence, Token token,
	                            bool silent = false);
	Expression *parseExpression(int precedence, bool silent = false);
	Statement * parseDeclaration();
	Array *     parseAllDeclarations();
	Statement * parseStatement();
	Statement * parseBlock(bool isStatic = false);
	String *    buildNextString(Token &t);
	InfixParselet *
	getInfixParselet(TokenType type); // return an infixparselet for the token
	// release the parselets
	void releaseAll();
	void mark() { GcObject::mark(declarations); }
};

class ParseException : public std::runtime_error {
  private:
	Token       t;
	const char *message;

  public:
	ParseException() : runtime_error("Error occurred while parsing!") {}
	ParseException(Token to, const char *msg)
	    : runtime_error(msg), t(to), message(msg) {}
	virtual const char *what() const throw() { return message; }
	Token               getToken() { return t; }
};
