# Copyright (c) 2004 Python Software Foundation.
# All rights reserved.

# Written by Eric Price <eprice at tjhsst.edu>
#    and Facundo Batista <facundo at taniquetil.com.ar>
#    and Raymond Hettinger <python at rcn.com>
#    and Aahz (aahz at pobox.com)
#    and Tim Peters

"""
These are the test cases for the Decimal module.

There are two groups of tests, Arithmetic and Behaviour. The former test
the Decimal arithmetic using the tests provided by Mike Cowlishaw. The latter
test the pythonic behaviour according to PEP 327.

Cowlishaw's tests can be downloaded from:

   www2.hursley.ibm.com/decimal/dectest.zip

This test module can be called from command line with one parameter (Arithmetic
or Behaviour) to test each part, or without parameter to test both parts. If
you're working through IDLE, you can import this test module and call test_main()
with the corresponding argument.
"""
from __future__ import with_statement

import unittest
import glob
import os, sys
import pickle, copy
from decimal import *
from test.test_support import (TestSkipped, run_unittest, run_doctest,
                               is_resource_enabled)
import random
try:
    import threading
except ImportError:
    threading = None

# Useful Test Constant
Signals = getcontext().flags.keys()

# Tests are built around these assumed context defaults.
# test_main() restores the original context.
def init():
    global ORIGINAL_CONTEXT
    ORIGINAL_CONTEXT = getcontext().copy()
    DefaultContext.prec = 9
    DefaultContext.rounding = ROUND_HALF_EVEN
    DefaultContext.traps = dict.fromkeys(Signals, 0)
    setcontext(DefaultContext)

TESTDATADIR = 'decimaltestdata'
if __name__ == '__main__':
    file = sys.argv[0]
else:
    file = __file__
testdir = os.path.dirname(file) or os.curdir
directory = testdir + os.sep + TESTDATADIR + os.sep

skip_expected = not os.path.isdir(directory)

# Make sure it actually raises errors when not expected and caught in flags
# Slower, since it runs some things several times.
EXTENDEDERRORTEST = False


#Map the test cases' error names to the actual errors

ErrorNames = {'clamped' : Clamped,
              'conversion_syntax' : InvalidOperation,
              'division_by_zero' : DivisionByZero,
              'division_impossible' : InvalidOperation,
              'division_undefined' : InvalidOperation,
              'inexact' : Inexact,
              'invalid_context' : InvalidOperation,
              'invalid_operation' : InvalidOperation,
              'overflow' : Overflow,
              'rounded' : Rounded,
              'subnormal' : Subnormal,
              'underflow' : Underflow}


def Nonfunction(*args):
    """Doesn't do anything."""
    return None

RoundingDict = {'ceiling' : ROUND_CEILING, #Maps test-case names to roundings.
                'down' : ROUND_DOWN,
                'floor' : ROUND_FLOOR,
                'half_down' : ROUND_HALF_DOWN,
                'half_even' : ROUND_HALF_EVEN,
                'half_up' : ROUND_HALF_UP,
                'up' : ROUND_UP}

# Name adapter to be able to change the Decimal and Context
# interface without changing the test files from Cowlishaw
nameAdapter = {'toeng':'to_eng_string',
               'tosci':'to_sci_string',
               'samequantum':'same_quantum',
               'tointegral':'to_integral',
               'remaindernear':'remainder_near',
               'divideint':'divide_int',
               'squareroot':'sqrt',
               'apply':'_apply',
              }

class DecimalTest(unittest.TestCase):
    """Class which tests the Decimal class against the test cases.

    Changed for unittest.
    """
    def setUp(self):
        self.context = Context()
        for key in DefaultContext.traps.keys():
            DefaultContext.traps[key] = 1
        self.ignore_list = ['#']
        # Basically, a # means return NaN InvalidOperation.
        # Different from a sNaN in trim

        self.ChangeDict = {'precision' : self.change_precision,
                      'rounding' : self.change_rounding_method,
                      'maxexponent' : self.change_max_exponent,
                      'minexponent' : self.change_min_exponent,
                      'clamp' : self.change_clamp}

    def tearDown(self):
        """Cleaning up enviroment."""
        # leaving context in original state
        for key in DefaultContext.traps.keys():
            DefaultContext.traps[key] = 0
        return

    def eval_file(self, file):
        global skip_expected
        if skip_expected:
            raise TestSkipped
            return
        for line in open(file).xreadlines():
            line = line.replace('\r\n', '').replace('\n', '')
            #print line
            try:
                t = self.eval_line(line)
            except InvalidOperation:
                print 'Error in test cases:'
                print line
                continue
            except DecimalException, exception:
                #Exception raised where there shoudn't have been one.
                self.fail('Exception "'+exception.__class__.__name__ + '" raised on line '+line)

        return

    def eval_line(self, s):
        if s.find(' -> ') >= 0 and s[:2] != '--' and not s.startswith('  --'):
            s = (s.split('->')[0] + '->' +
                 s.split('->')[1].split('--')[0]).strip()
        else:
            s = s.split('--')[0].strip()

        for ignore in self.ignore_list:
            if s.find(ignore) >= 0:
                #print s.split()[0], 'NotImplemented--', ignore
                return
        if not s:
            return
        elif ':' in s:
            return self.eval_directive(s)
        else:
            return self.eval_equation(s)

    def eval_directive(self, s):
        funct, value = map(lambda x: x.strip().lower(), s.split(':'))
        if funct == 'rounding':
            value = RoundingDict[value]
        else:
            try:
                value = int(value)
            except ValueError:
                pass

        funct = self.ChangeDict.get(funct, Nonfunction)
        funct(value)

    def eval_equation(self, s):
        #global DEFAULT_PRECISION
        #print DEFAULT_PRECISION

        if not TEST_ALL and random.random() < 0.90:
            return

        try:
            Sides = s.split('->')
            L = Sides[0].strip().split()
            id = L[0]
#            print id,
            funct = L[1].lower()
            valstemp = L[2:]
            L = Sides[1].strip().split()
            ans = L[0]
            exceptions = L[1:]
        except (TypeError, AttributeError, IndexError):
            raise InvalidOperation
        def FixQuotes(val):
            val = val.replace("''", 'SingleQuote').replace('""', 'DoubleQuote')
            val = val.replace("'", '').replace('"', '')
            val = val.replace('SingleQuote', "'").replace('DoubleQuote', '"')
            return val
        fname = nameAdapter.get(funct, funct)
        if fname == 'rescale':
            return
        funct = getattr(self.context, fname)
        vals = []
        conglomerate = ''
        quote = 0
        theirexceptions = [ErrorNames[x.lower()] for x in exceptions]

        for exception in Signals:
            self.context.traps[exception] = 1 #Catch these bugs...
        for exception in theirexceptions:
            self.context.traps[exception] = 0
        for i, val in enumerate(valstemp):
            if val.count("'") % 2 == 1:
                quote = 1 - quote
            if quote:
                conglomerate = conglomerate + ' ' + val
                continue
            else:
                val = conglomerate + val
                conglomerate = ''
            v = FixQuotes(val)
            if fname in ('to_sci_string', 'to_eng_string'):
                if EXTENDEDERRORTEST:
                    for error in theirexceptions:
                        self.context.traps[error] = 1
                        try:
                            funct(self.context.create_decimal(v))
                        except error:
                            pass
                        except Signals, e:
                            self.fail("Raised %s in %s when %s disabled" % \
                                      (e, s, error))
                        else:
                            self.fail("Did not raise %s in %s" % (error, s))
                        self.context.traps[error] = 0
                v = self.context.create_decimal(v)
            else:
                v = Decimal(v)
            vals.append(v)

        ans = FixQuotes(ans)

        if EXTENDEDERRORTEST and fname not in ('to_sci_string', 'to_eng_string'):
            for error in theirexceptions:
                self.context.traps[error] = 1
                try:
                    funct(*vals)
                except error:
                    pass
                except Signals, e:
                    self.fail("Raised %s in %s when %s disabled" % \
                              (e, s, error))
                else:
                    self.fail("Did not raise %s in %s" % (error, s))
                self.context.traps[error] = 0
        try:
            result = str(funct(*vals))
            if fname == 'same_quantum':
                result = str(int(eval(result))) # 'True', 'False' -> '1', '0'
        except Signals, error:
            self.fail("Raised %s in %s" % (error, s))
        except: #Catch any error long enough to state the test case.
            print "ERROR:", s
            raise

        myexceptions = self.getexceptions()
        self.context.clear_flags()

        myexceptions.sort()
        theirexceptions.sort()

        self.assertEqual(result, ans,
                         'Incorrect answer for ' + s + ' -- got ' + result)
        self.assertEqual(myexceptions, theirexceptions,
                         'Incorrect flags set in ' + s + ' -- got ' \
                         + str(myexceptions))
        return

    def getexceptions(self):
        return [e for e in Signals if self.context.flags[e]]

    def change_precision(self, prec):
        self.context.prec = prec
    def change_rounding_method(self, rounding):
        self.context.rounding = rounding
    def change_min_exponent(self, exp):
        self.context.Emin = exp
    def change_max_exponent(self, exp):
        self.context.Emax = exp
    def change_clamp(self, clamp):
        self.context._clamp = clamp

# Dynamically build custom test definition for each file in the test
# directory and add the definitions to the DecimalTest class.  This
# procedure insures that new files do not get skipped.
for filename in os.listdir(directory):
    if '.decTest' not in filename:
        continue
    head, tail = filename.split('.')
    tester = lambda self, f=filename: self.eval_file(directory + f)
    setattr(DecimalTest, 'test_' + head, tester)
    del filename, head, tail, tester



# The following classes test the behaviour of Decimal according to PEP 327

class DecimalExplicitConstructionTest(unittest.TestCase):
    '''Unit tests for Explicit Construction cases of Decimal.'''

    def test_explicit_empty(self):
        self.assertEqual(Decimal(), Decimal("0"))

    def test_explicit_from_None(self):
        self.assertRaises(TypeError, Decimal, None)

    def test_explicit_from_int(self):

        #positive
        d = Decimal(45)
        self.assertEqual(str(d), '45')

        #very large positive
        d = Decimal(500000123)
        self.assertEqual(str(d), '500000123')

        #negative
        d = Decimal(-45)
        self.assertEqual(str(d), '-45')

        #zero
        d = Decimal(0)
        self.assertEqual(str(d), '0')

    def test_explicit_from_string(self):

        #empty
        self.assertEqual(str(Decimal('')), 'NaN')

        #int
        self.assertEqual(str(Decimal('45')), '45')

        #float
        self.assertEqual(str(Decimal('45.34')), '45.34')

        #engineer notation
        self.assertEqual(str(Decimal('45e2')), '4.5E+3')

        #just not a number
        self.assertEqual(str(Decimal('ugly')), 'NaN')

    def test_explicit_from_tuples(self):

        #zero
        d = Decimal( (0, (0,), 0) )
        self.assertEqual(str(d), '0')

        #int
        d = Decimal( (1, (4, 5), 0) )
        self.assertEqual(str(d), '-45')

        #float
        d = Decimal( (0, (4, 5, 3, 4), -2) )
        self.assertEqual(str(d), '45.34')

        #weird
        d = Decimal( (1, (4, 3, 4, 9, 1, 3, 5, 3, 4), -25) )
        self.assertEqual(str(d), '-4.34913534E-17')

        #wrong number of items
        self.assertRaises(ValueError, Decimal, (1, (4, 3, 4, 9, 1)) )

        #bad sign
        self.assertRaises(ValueError, Decimal, (8, (4, 3, 4, 9, 1), 2) )

        #bad exp
        self.assertRaises(ValueError, Decimal, (1, (4, 3, 4, 9, 1), 'wrong!') )

        #bad coefficients
        self.assertRaises(ValueError, Decimal, (1, (4, 3, 4, None, 1), 2) )
        self.assertRaises(ValueError, Decimal, (1, (4, -3, 4, 9, 1), 2) )

    def test_explicit_from_Decimal(self):

        #positive
        d = Decimal(45)
        e = Decimal(d)
        self.assertEqual(str(e), '45')
        self.assertNotEqual(id(d), id(e))

        #very large positive
        d = Decimal(500000123)
        e = Decimal(d)
        self.assertEqual(str(e), '500000123')
        self.assertNotEqual(id(d), id(e))

        #negative
        d = Decimal(-45)
        e = Decimal(d)
        self.assertEqual(str(e), '-45')
        self.assertNotEqual(id(d), id(e))

        #zero
        d = Decimal(0)
        e = Decimal(d)
        self.assertEqual(str(e), '0')
        self.assertNotEqual(id(d), id(e))

    def test_explicit_context_create_decimal(self):

        nc = copy.copy(getcontext())
        nc.prec = 3

        # empty
        d = Decimal()
        self.assertEqual(str(d), '0')
        d = nc.create_decimal()
        self.assertEqual(str(d), '0')

        # from None
        self.assertRaises(TypeError, nc.create_decimal, None)

        # from int
        d = nc.create_decimal(456)
        self.failUnless(isinstance(d, Decimal))
        self.assertEqual(nc.create_decimal(45678),
                         nc.create_decimal('457E+2'))

        # from string
        d = Decimal('456789')
        self.assertEqual(str(d), '456789')
        d = nc.create_decimal('456789')
        self.assertEqual(str(d), '4.57E+5')

        # from tuples
        d = Decimal( (1, (4, 3, 4, 9, 1, 3, 5, 3, 4), -25) )
        self.assertEqual(str(d), '-4.34913534E-17')
        d = nc.create_decimal( (1, (4, 3, 4, 9, 1, 3, 5, 3, 4), -25) )
        self.assertEqual(str(d), '-4.35E-17')

        # from Decimal
        prevdec = Decimal(500000123)
        d = Decimal(prevdec)
        self.assertEqual(str(d), '500000123')
        d = nc.create_decimal(prevdec)
        self.assertEqual(str(d), '5.00E+8')


class DecimalImplicitConstructionTest(unittest.TestCase):
    '''Unit tests for Implicit Construction cases of Decimal.'''

    def test_implicit_from_None(self):
        self.assertRaises(TypeError, eval, 'Decimal(5) + None', globals())

    def test_implicit_from_int(self):
        #normal
        self.assertEqual(str(Decimal(5) + 45), '50')
        #exceeding precision
        self.assertEqual(Decimal(5) + 123456789000, Decimal(123456789000))

    def test_implicit_from_string(self):
        self.assertRaises(TypeError, eval, 'Decimal(5) + "3"', globals())

    def test_implicit_from_float(self):
        self.assertRaises(TypeError, eval, 'Decimal(5) + 2.2', globals())

    def test_implicit_from_Decimal(self):
        self.assertEqual(Decimal(5) + Decimal(45), Decimal(50))

    def test_rop(self):
        # Allow other classes to be trained to interact with Decimals
        class E:
            def __divmod__(self, other):
                return 'divmod ' + str(other)
            def __rdivmod__(self, other):
                return str(other) + ' rdivmod'
            def __lt__(self, other):
                return 'lt ' + str(other)
            def __gt__(self, other):
                return 'gt ' + str(other)
            def __le__(self, other):
                return 'le ' + str(other)
            def __ge__(self, other):
                return 'ge ' + str(other)
            def __eq__(self, other):
                return 'eq ' + str(other)
            def __ne__(self, other):
                return 'ne ' + str(other)

        self.assertEqual(divmod(E(), Decimal(10)), 'divmod 10')
        self.assertEqual(divmod(Decimal(10), E()), '10 rdivmod')
        self.assertEqual(eval('Decimal(10) < E()'), 'gt 10')
        self.assertEqual(eval('Decimal(10) > E()'), 'lt 10')
        self.assertEqual(eval('Decimal(10) <= E()'), 'ge 10')
        self.assertEqual(eval('Decimal(10) >= E()'), 'le 10')
        self.assertEqual(eval('Decimal(10) == E()'), 'eq 10')
        self.assertEqual(eval('Decimal(10) != E()'), 'ne 10')

        # insert operator methods and then exercise them
        oplist = [
            ('+', '__add__', '__radd__'),
            ('-', '__sub__', '__rsub__'),
            ('*', '__mul__', '__rmul__'),
            ('%', '__mod__', '__rmod__'),
            ('//', '__floordiv__', '__rfloordiv__'),
            ('**', '__pow__', '__rpow__')
        ]
        if 1/2 == 0:
            # testing with classic division, so add __div__
            oplist.append(('/', '__div__', '__rdiv__'))
        else:
            # testing with -Qnew, so add __truediv__
            oplist.append(('/', '__truediv__', '__rtruediv__'))

        for sym, lop, rop in oplist:
            setattr(E, lop, lambda self, other: 'str' + lop + str(other))
            setattr(E, rop, lambda self, other: str(other) + rop + 'str')
            self.assertEqual(eval('E()' + sym + 'Decimal(10)'),
                             'str' + lop + '10')
            self.assertEqual(eval('Decimal(10)' + sym + 'E()'),
                             '10' + rop + 'str')

class DecimalArithmeticOperatorsTest(unittest.TestCase):
    '''Unit tests for all arithmetic operators, binary and unary.'''

    def test_addition(self):

        d1 = Decimal('-11.1')
        d2 = Decimal('22.2')

        #two Decimals
        self.assertEqual(d1+d2, Decimal('11.1'))
        self.assertEqual(d2+d1, Decimal('11.1'))

        #with other type, left
        c = d1 + 5
        self.assertEqual(c, Decimal('-6.1'))
        self.assertEqual(type(c), type(d1))

        #with other type, right
        c = 5 + d1
        self.assertEqual(c, Decimal('-6.1'))
        self.assertEqual(type(c), type(d1))

        #inline with decimal
        d1 += d2
        self.assertEqual(d1, Decimal('11.1'))

        #inline with other type
        d1 += 5
        self.assertEqual(d1, Decimal('16.1'))

    def test_subtraction(self):

        d1 = Decimal('-11.1')
        d2 = Decimal('22.2')

        #two Decimals
        self.assertEqual(d1-d2, Decimal('-33.3'))
        self.assertEqual(d2-d1, Decimal('33.3'))

        #with other type, left
        c = d1 - 5
        self.assertEqual(c, Decimal('-16.1'))
        self.assertEqual(type(c), type(d1))

        #with other type, right
        c = 5 - d1
        self.assertEqual(c, Decimal('16.1'))
        self.assertEqual(type(c), type(d1))

        #inline with decimal
        d1 -= d2
        self.assertEqual(d1, Decimal('-33.3'))

        #inline with other type
        d1 -= 5
        self.assertEqual(d1, Decimal('-38.3'))

    def test_multiplication(self):

        d1 = Decimal('-5')
        d2 = Decimal('3')

        #two Decimals
        self.assertEqual(d1*d2, Decimal('-15'))
        self.assertEqual(d2*d1, Decimal('-15'))

        #with other type, left
        c = d1 * 5
        self.assertEqual(c, Decimal('-25'))
        self.assertEqual(type(c), type(d1))

        #with other type, right
        c = 5 * d1
        self.assertEqual(c, Decimal('-25'))
        self.assertEqual(type(c), type(d1))

        #inline with decimal
        d1 *= d2
        self.assertEqual(d1, Decimal('-15'))

        #inline with other type
        d1 *= 5
        self.assertEqual(d1, Decimal('-75'))

    def test_division(self):

        d1 = Decimal('-5')
        d2 = Decimal('2')

        #two Decimals
        self.assertEqual(d1/d2, Decimal('-2.5'))
        self.assertEqual(d2/d1, Decimal('-0.4'))

        #with other type, left
        c = d1 / 4
        self.assertEqual(c, Decimal('-1.25'))
        self.assertEqual(type(c), type(d1))

        #with other type, right
        c = 4 / d1
        self.assertEqual(c, Decimal('-0.8'))
        self.assertEqual(type(c), type(d1))

        #inline with decimal
        d1 /= d2
        self.assertEqual(d1, Decimal('-2.5'))

        #inline with other type
        d1 /= 4
        self.assertEqual(d1, Decimal('-0.625'))

    def test_floor_division(self):

        d1 = Decimal('5')
        d2 = Decimal('2')

        #two Decimals
        self.assertEqual(d1//d2, Decimal('2'))
        self.assertEqual(d2//d1, Decimal('0'))

        #with other type, left
        c = d1 // 4
        self.assertEqual(c, Decimal('1'))
        self.assertEqual(type(c), type(d1))

        #with other type, right
        c = 7 // d1
        self.assertEqual(c, Decimal('1'))
        self.assertEqual(type(c), type(d1))

        #inline with decimal
        d1 //= d2
        self.assertEqual(d1, Decimal('2'))

        #inline with other type
        d1 //= 2
        self.assertEqual(d1, Decimal('1'))

    def test_powering(self):

        d1 = Decimal('5')
        d2 = Decimal('2')

        #two Decimals
        self.assertEqual(d1**d2, Decimal('25'))
        self.assertEqual(d2**d1, Decimal('32'))

        #with other type, left
        c = d1 ** 4
        self.assertEqual(c, Decimal('625'))
        self.assertEqual(type(c), type(d1))

        #with other type, right
        c = 7 ** d1
        self.assertEqual(c, Decimal('16807'))
        self.assertEqual(type(c), type(d1))

        #inline with decimal
        d1 **= d2
        self.assertEqual(d1, Decimal('25'))

        #inline with other type
        d1 **= 4
        self.assertEqual(d1, Decimal('390625'))

    def test_module(self):

        d1 = Decimal('5')
        d2 = Decimal('2')

        #two Decimals
        self.assertEqual(d1%d2, Decimal('1'))
        self.assertEqual(d2%d1, Decimal('2'))

        #with other type, left
        c = d1 % 4
        self.assertEqual(c, Decimal('1'))
        self.assertEqual(type(c), type(d1))

        #with other type, right
        c = 7 % d1
        self.assertEqual(c, Decimal('2'))
        self.assertEqual(type(c), type(d1))

        #inline with decimal
        d1 %= d2
        self.assertEqual(d1, Decimal('1'))

        #inline with other type
        d1 %= 4
        self.assertEqual(d1, Decimal('1'))

    def test_floor_div_module(self):

        d1 = Decimal('5')
        d2 = Decimal('2')

        #two Decimals
        (p, q) = divmod(d1, d2)
        self.assertEqual(p, Decimal('2'))
        self.assertEqual(q, Decimal('1'))
        self.assertEqual(type(p), type(d1))
        self.assertEqual(type(q), type(d1))

        #with other type, left
        (p, q) = divmod(d1, 4)
        self.assertEqual(p, Decimal('1'))
        self.assertEqual(q, Decimal('1'))
        self.assertEqual(type(p), type(d1))
        self.assertEqual(type(q), type(d1))

        #with other type, right
        (p, q) = divmod(7, d1)
        self.assertEqual(p, Decimal('1'))
        self.assertEqual(q, Decimal('2'))
        self.assertEqual(type(p), type(d1))
        self.assertEqual(type(q), type(d1))

    def test_unary_operators(self):
        self.assertEqual(+Decimal(45), Decimal(+45))           #  +
        self.assertEqual(-Decimal(45), Decimal(-45))           #  -
        self.assertEqual(abs(Decimal(45)), abs(Decimal(-45)))  # abs


# The following are two functions used to test threading in the next class

def thfunc1(cls):
    d1 = Decimal(1)
    d3 = Decimal(3)
    cls.assertEqual(d1/d3, Decimal('0.333333333'))
    cls.synchro.wait()
    cls.assertEqual(d1/d3, Decimal('0.333333333'))
    cls.finish1.set()
    return

def thfunc2(cls):
    d1 = Decimal(1)
    d3 = Decimal(3)
    cls.assertEqual(d1/d3, Decimal('0.333333333'))
    thiscontext = getcontext()
    thiscontext.prec = 18
    cls.assertEqual(d1/d3, Decimal('0.333333333333333333'))
    cls.synchro.set()
    cls.finish2.set()
    return


class DecimalUseOfContextTest(unittest.TestCase):
    '''Unit tests for Use of Context cases in Decimal.'''

    try:
        import threading
    except ImportError:
        threading = None

    # Take care executing this test from IDLE, there's an issue in threading
    # that hangs IDLE and I couldn't find it

    def test_threading(self):
        #Test the "threading isolation" of a Context.

        self.synchro = threading.Event()
        self.finish1 = threading.Event()
        self.finish2 = threading.Event()

        th1 = threading.Thread(target=thfunc1, args=(self,))
        th2 = threading.Thread(target=thfunc2, args=(self,))

        th1.start()
        th2.start()

        self.finish1.wait()
        self.finish1.wait()
        return

    if threading is None:
        del test_threading


class DecimalUsabilityTest(unittest.TestCase):
    '''Unit tests for Usability cases of Decimal.'''

    def test_comparison_operators(self):

        da = Decimal('23.42')
        db = Decimal('23.42')
        dc = Decimal('45')

        #two Decimals
        self.failUnless(dc > da)
        self.failUnless(dc >= da)
        self.failUnless(da < dc)
        self.failUnless(da <= dc)
        self.failUnless(da == db)
        self.failUnless(da != dc)
        self.failUnless(da <= db)
        self.failUnless(da >= db)
        self.assertEqual(cmp(dc,da), 1)
        self.assertEqual(cmp(da,dc), -1)
        self.assertEqual(cmp(da,db), 0)

        #a Decimal and an int
        self.failUnless(dc > 23)
        self.failUnless(23 < dc)
        self.failUnless(dc == 45)
        self.assertEqual(cmp(dc,23), 1)
        self.assertEqual(cmp(23,dc), -1)
        self.assertEqual(cmp(dc,45), 0)

        #a Decimal and uncomparable
        self.assertNotEqual(da, 'ugly')
        self.assertNotEqual(da, 32.7)
        self.assertNotEqual(da, object())
        self.assertNotEqual(da, object)

        # sortable
        a = map(Decimal, xrange(100))
        b =  a[:]
        random.shuffle(a)
        a.sort()
        self.assertEqual(a, b)

    def test_copy_and_deepcopy_methods(self):
        d = Decimal('43.24')
        c = copy.copy(d)
        self.assertEqual(id(c), id(d))
        dc = copy.deepcopy(d)
        self.assertEqual(id(dc), id(d))

    def test_hash_method(self):
        #just that it's hashable
        hash(Decimal(23))
        #the same hash that to an int
        self.assertEqual(hash(Decimal(23)), hash(23))
        self.assertRaises(TypeError, hash, Decimal('NaN'))
        self.assert_(hash(Decimal('Inf')))
        self.assert_(hash(Decimal('-Inf')))

    def test_min_and_max_methods(self):

        d1 = Decimal('15.32')
        d2 = Decimal('28.5')
        l1 = 15
        l2 = 28

        #between Decimals
        self.failUnless(min(d1,d2) is d1)
        self.failUnless(min(d2,d1) is d1)
        self.failUnless(max(d1,d2) is d2)
        self.failUnless(max(d2,d1) is d2)

        #between Decimal and long
        self.failUnless(min(d1,l2) is d1)
        self.failUnless(min(l2,d1) is d1)
        self.failUnless(max(l1,d2) is d2)
        self.failUnless(max(d2,l1) is d2)

    def test_as_nonzero(self):
        #as false
        self.failIf(Decimal(0))
        #as true
        self.failUnless(Decimal('0.372'))

    def test_tostring_methods(self):
        #Test str and repr methods.

        d = Decimal('15.32')
        self.assertEqual(str(d), '15.32')               # str
        self.assertEqual(repr(d), 'Decimal("15.32")')   # repr

    def test_tonum_methods(self):
        #Test float, int and long methods.

        d1 = Decimal('66')
        d2 = Decimal('15.32')

        #int
        self.assertEqual(int(d1), 66)
        self.assertEqual(int(d2), 15)

        #long
        self.assertEqual(long(d1), 66)
        self.assertEqual(long(d2), 15)

        #float
        self.assertEqual(float(d1), 66)
        self.assertEqual(float(d2), 15.32)

    def test_eval_round_trip(self):

        #with zero
        d = Decimal( (0, (0,), 0) )
        self.assertEqual(d, eval(repr(d)))

        #int
        d = Decimal( (1, (4, 5), 0) )
        self.assertEqual(d, eval(repr(d)))

        #float
        d = Decimal( (0, (4, 5, 3, 4), -2) )
        self.assertEqual(d, eval(repr(d)))

        #weird
        d = Decimal( (1, (4, 3, 4, 9, 1, 3, 5, 3, 4), -25) )
        self.assertEqual(d, eval(repr(d)))

    def test_as_tuple(self):

        #with zero
        d = Decimal(0)
        self.assertEqual(d.as_tuple(), (0, (0,), 0) )

        #int
        d = Decimal(-45)
        self.assertEqual(d.as_tuple(), (1, (4, 5), 0) )

        #complicated string
        d = Decimal("-4.34913534E-17")
        self.assertEqual(d.as_tuple(), (1, (4, 3, 4, 9, 1, 3, 5, 3, 4), -25) )

        #inf
        d = Decimal("Infinity")
        self.assertEqual(d.as_tuple(), (0, (0,), 'F') )

    def test_immutability_operations(self):
        # Do operations and check that it didn't change change internal objects.

        d1 = Decimal('-25e55')
        b1 = Decimal('-25e55')
        d2 = Decimal('33e-33')
        b2 = Decimal('33e-33')

        def checkSameDec(operation, useOther=False):
            if useOther:
                eval("d1." + operation + "(d2)")
                self.assertEqual(d1._sign, b1._sign)
                self.assertEqual(d1._int, b1._int)
                self.assertEqual(d1._exp, b1._exp)
                self.assertEqual(d2._sign, b2._sign)
                self.assertEqual(d2._int, b2._int)
                self.assertEqual(d2._exp, b2._exp)
            else:
                eval("d1." + operation + "()")
                self.assertEqual(d1._sign, b1._sign)
                self.assertEqual(d1._int, b1._int)
                self.assertEqual(d1._exp, b1._exp)
            return

        Decimal(d1)
        self.assertEqual(d1._sign, b1._sign)
        self.assertEqual(d1._int, b1._int)
        self.assertEqual(d1._exp, b1._exp)

        checkSameDec("__abs__")
        checkSameDec("__add__", True)
        checkSameDec("__div__", True)
        checkSameDec("__divmod__", True)
        checkSameDec("__cmp__", True)
        checkSameDec("__float__")
        checkSameDec("__floordiv__", True)
        checkSameDec("__hash__")
        checkSameDec("__int__")
        checkSameDec("__long__")
        checkSameDec("__mod__", True)
        checkSameDec("__mul__", True)
        checkSameDec("__neg__")
        checkSameDec("__nonzero__")
        checkSameDec("__pos__")
        checkSameDec("__pow__", True)
        checkSameDec("__radd__", True)
        checkSameDec("__rdiv__", True)
        checkSameDec("__rdivmod__", True)
        checkSameDec("__repr__")
        checkSameDec("__rfloordiv__", True)
        checkSameDec("__rmod__", True)
        checkSameDec("__rmul__", True)
        checkSameDec("__rpow__", True)
        checkSameDec("__rsub__", True)
        checkSameDec("__str__")
        checkSameDec("__sub__", True)
        checkSameDec("__truediv__", True)
        checkSameDec("adjusted")
        checkSameDec("as_tuple")
        checkSameDec("compare", True)
        checkSameDec("max", True)
        checkSameDec("min", True)
        checkSameDec("normalize")
        checkSameDec("quantize", True)
        checkSameDec("remainder_near", True)
        checkSameDec("same_quantum", True)
        checkSameDec("sqrt")
        checkSameDec("to_eng_string")
        checkSameDec("to_integral")

class DecimalPythonAPItests(unittest.TestCase):

    def test_pickle(self):
        d = Decimal('-3.141590000')
        p = pickle.dumps(d)
        e = pickle.loads(p)
        self.assertEqual(d, e)

    def test_int(self):
        for x in range(-250, 250):
            s = '%0.2f' % (x / 100.0)
            # should work the same as for floats
            self.assertEqual(int(Decimal(s)), int(float(s)))
            # should work the same as to_integral in the ROUND_DOWN mode
            d = Decimal(s)
            r = d.to_integral(ROUND_DOWN)
            self.assertEqual(Decimal(int(d)), r)

class ContextAPItests(unittest.TestCase):

    def test_pickle(self):
        c = Context()
        e = pickle.loads(pickle.dumps(c))
        for k in vars(c):
            v1 = vars(c)[k]
            v2 = vars(e)[k]
            self.assertEqual(v1, v2)

    def test_equality_with_other_types(self):
        self.assert_(Decimal(10) in ['a', 1.0, Decimal(10), (1,2), {}])
        self.assert_(Decimal(10) not in ['a', 1.0, (1,2), {}])

    def test_copy(self):
        # All copies should be deep
        c = Context()
        d = c.copy()
        self.assertNotEqual(id(c), id(d))
        self.assertNotEqual(id(c.flags), id(d.flags))
        self.assertNotEqual(id(c.traps), id(d.traps))

class WithStatementTest(unittest.TestCase):
    # Can't do these as docstrings until Python 2.6
    # as doctest can't handle __future__ statements

    def test_localcontext(self):
        # Use a copy of the current context in the block
        orig_ctx = getcontext()
        with localcontext() as enter_ctx:
            set_ctx = getcontext()
        final_ctx = getcontext()
        self.assert_(orig_ctx is final_ctx, 'did not restore context correctly')
        self.assert_(orig_ctx is not set_ctx, 'did not copy the context')
        self.assert_(set_ctx is enter_ctx, '__enter__ returned wrong context')

    def test_localcontextarg(self):
        # Use a copy of the supplied context in the block
        orig_ctx = getcontext()
        new_ctx = Context(prec=42)
        with localcontext(new_ctx) as enter_ctx:
            set_ctx = getcontext()
        final_ctx = getcontext()
        self.assert_(orig_ctx is final_ctx, 'did not restore context correctly')
        self.assert_(set_ctx.prec == new_ctx.prec, 'did not set correct context')
        self.assert_(new_ctx is not set_ctx, 'did not copy the context')
        self.assert_(set_ctx is enter_ctx, '__enter__ returned wrong context')

def test_main(arith=False, verbose=None):
    """ Execute the tests.

    Runs all arithmetic tests if arith is True or if the "decimal" resource
    is enabled in regrtest.py
    """

    init()
    global TEST_ALL
    TEST_ALL = arith or is_resource_enabled('decimal')

    test_classes = [
        DecimalExplicitConstructionTest,
        DecimalImplicitConstructionTest,
        DecimalArithmeticOperatorsTest,
        DecimalUseOfContextTest,
        DecimalUsabilityTest,
        DecimalPythonAPItests,
        ContextAPItests,
        DecimalTest,
        WithStatementTest,
    ]

    try:
        run_unittest(*test_classes)
        import decimal as DecimalModule
        run_doctest(DecimalModule, verbose)
    finally:
        setcontext(ORIGINAL_CONTEXT)

if __name__ == '__main__':
    # Calling with no arguments runs all tests.
    # Calling with "Skip" will skip over 90% of the arithmetic tests.
    if len(sys.argv) == 1:
        test_main(arith=True, verbose=True)
    elif len(sys.argv) == 2:
        arith = sys.argv[1].lower() != 'skip'
        test_main(arith=arith, verbose=True)
    else:
        raise ValueError("test called with wrong arguments, use test_Decimal [Skip]")
