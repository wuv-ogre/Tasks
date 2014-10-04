// This file is part of Tasks.
//
// Tasks is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Tasks is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with Tasks.  If not, see <http://www.gnu.org/licenses/>.

// associated header
#include "QPSolver.h"

// includes
// std
#include <iostream>
#include <limits>
#include <cmath>

// RBDyn
#include <RBDyn/MultiBody.h>
#include <RBDyn/MultiBodyConfig.h>

// Tasks
#include "GenQPSolver.h"


namespace tasks
{

namespace qp
{



/**
	*													QPSolver
	*/



QPSolver::QPSolver():
	constr_(),
	eqConstr_(),
	inEqConstr_(),
	genInEqConstr_(),
	boundConstr_(),
	tasks_(),
	maxEqLines_(0),
	maxInEqLines_(0),
	maxGenInEqLines_(0),
	solver_(createQPSolver("LSSOL"))
{
}


// must declare it in cpp because of GenQPSolver fwd declarition
QPSolver::~QPSolver()
{}


bool QPSolver::solve(const std::vector<rbd::MultiBody>& mbs,
										std::vector<rbd::MultiBodyConfig>& mbcs)
{
	preUpdate(mbs, mbcs);

	bool success = solver_->solve();

	if(!success)
	{
		solver_->errorMsg(mbs,
										 tasks_, eqConstr_, inEqConstr_,
										 genInEqConstr_, boundConstr_,
										 std::cerr) << std::endl;
	}

	postUpdate(mbs, mbcs, success);

	return success;
}


template <typename T>
int accumMaxLines(int acc, T* constr)
{
	return acc + constr_traits<T>::maxLines(constr);
}


void QPSolver::updateConstrSize()
{
	maxEqLines_ = std::accumulate(eqConstr_.begin(), eqConstr_.end(), 0,
		accumMaxLines<Equality>);
	maxInEqLines_ = std::accumulate(inEqConstr_.begin(), inEqConstr_.end(), 0,
		accumMaxLines<Inequality>);
	maxGenInEqLines_ = std::accumulate(genInEqConstr_.begin(), genInEqConstr_.end(),
		0, accumMaxLines<GenInequality>);

	solver_->updateSize(data_.nrVars_, maxEqLines_, maxInEqLines_, maxGenInEqLines_);
}


void QPSolver::nrVars(const std::vector<rbd::MultiBody>& mbs,
	std::vector<UnilateralContact> uni,
	std::vector<BilateralContact> bi)
{
	data_.alphaD_.resize(mbs.size());
	data_.alphaDBegin_.resize(mbs.size());

	data_.uniCont_ = std::move(uni);
	data_.biCont_ = std::move(bi);

	int nrContacts = data_.nrContacts();

	data_.lambda_.resize(nrContacts);
	data_.lambdaBegin_.resize(nrContacts);

	data_.normalAccB_.resize(mbs.size());

	int cumAlphaD = 0;
	for(std::size_t r = 0; r < mbs.size(); ++r)
	{
		const rbd::MultiBody& mb = mbs[r];
		data_.alphaD_[r] = mb.nrDof();
		data_.alphaDBegin_[r] = cumAlphaD;
		data_.normalAccB_[r].resize(mb.nrBodies());
		cumAlphaD += mb.nrDof();
	}
	data_.totalAlphaD_ = cumAlphaD;

	int cumLambda = cumAlphaD;
	int cIndex = 0;
	data_.allCont_.clear();
	// counting unilateral contact
	for(const UnilateralContact& c: data_.uniCont_)
	{
		data_.lambdaBegin_[cIndex] = cumLambda;
		int lambda = 0;
		for(std::size_t p = 0; p < c.r1Points.size(); ++p)
		{
			lambda += c.nrLambda(int(p));
		}
		data_.lambda_[cIndex] = lambda;
		cumLambda += lambda;
		++cIndex;

		data_.allCont_.emplace_back(c);
	}
	data_.nrUniLambda_ = cumLambda - cumAlphaD;

	// counting bilateral contact
	for(const BilateralContact& c: data_.biCont_)
	{
		data_.lambdaBegin_[cIndex] = cumLambda;
		int lambda = 0;
		for(std::size_t p = 0; p < c.r1Points.size(); ++p)
		{
			lambda += c.nrLambda(int(p));
		}
		data_.lambda_[cIndex] = lambda;
		cumLambda += lambda;
		++cIndex;

		data_.allCont_.emplace_back(c);
	}
	data_.nrBiLambda_ = cumLambda - data_.nrUniLambda_ - cumAlphaD;

	data_.totalLambda_ = data_.nrUniLambda_ + data_.nrBiLambda_;
	data_.nrVars_ = data_.totalAlphaD_ + data_.totalLambda_;

	for(Task* t: tasks_)
	{
		t->updateNrVars(mbs, data_);
	}

	for(Constraint* c: constr_)
	{
		c->updateNrVars(mbs, data_);
	}

	solver_->updateSize(data_.nrVars_, maxEqLines_, maxInEqLines_, maxGenInEqLines_);
}


int QPSolver::nrVars() const
{
	return data_.nrVars_;
}


void QPSolver::updateTasksNrVars(const std::vector<rbd::MultiBody>& mbs) const
{
	for(Task* t: tasks_)
	{
		t->updateNrVars(mbs, data_);
	}
}


void QPSolver::updateConstrsNrVars(const std::vector<rbd::MultiBody>& mbs) const
{
	for(Constraint* c: constr_)
	{
		c->updateNrVars(mbs, data_);
	}
}


void QPSolver::updateNrVars(const std::vector<rbd::MultiBody>& mbs) const
{
	updateTasksNrVars(mbs);
	updateConstrsNrVars(mbs);
}


void QPSolver::addEqualityConstraint(Equality* co)
{
	eqConstr_.push_back(co);
}


void QPSolver::removeEqualityConstraint(Equality* co)
{
	eqConstr_.erase(std::find(eqConstr_.begin(), eqConstr_.end(), co));
}


int QPSolver::nrEqualityConstraints() const
{
	return static_cast<int>(eqConstr_.size());
}


void QPSolver::addInequalityConstraint(Inequality* co)
{
	inEqConstr_.push_back(co);
}


void QPSolver::removeInequalityConstraint(Inequality* co)
{
	inEqConstr_.erase(std::find(inEqConstr_.begin(), inEqConstr_.end(), co));
}


int QPSolver::nrInequalityConstraints() const
{
	return static_cast<int>(inEqConstr_.size());
}


void QPSolver::addGenInequalityConstraint(GenInequality* co)
{
	genInEqConstr_.push_back(co);
}


void QPSolver::removeGenInequalityConstraint(GenInequality* co)
{
	genInEqConstr_.erase(std::find(genInEqConstr_.begin(), genInEqConstr_.end(), co));
}


int QPSolver::nrGenInequalityConstraints() const
{
	return static_cast<int>(genInEqConstr_.size());
}


void QPSolver::addBoundConstraint(Bound* co)
{
	boundConstr_.push_back(co);
}


void QPSolver::removeBoundConstraint(Bound* co)
{
	boundConstr_.erase(std::find(boundConstr_.begin(), boundConstr_.end(), co));
}


int QPSolver::nrBoundConstraints() const
{
	return static_cast<int>(boundConstr_.size());
}


void QPSolver::addConstraint(Constraint* co)
{
	if(std::find(constr_.begin(), constr_.end(), co) == constr_.end())
	{
		constr_.push_back(co);
	}
}


void QPSolver::removeConstraint(Constraint* co)
{
	auto it = std::find(constr_.begin(), constr_.end(), co);
	if(it != constr_.end())
	{
		constr_.erase(it);
	}
}

int QPSolver::nrConstraints() const
{
	return static_cast<int>(constr_.size());
}


void QPSolver::addTask(Task* task)
{
	if(std::find(tasks_.begin(), tasks_.end(), task) == tasks_.end())
	{
		tasks_.push_back(task);
	}
}


void QPSolver::removeTask(Task* task)
{
	auto it = std::find(tasks_.begin(), tasks_.end(), task);
	if(it != tasks_.end())
	{
		tasks_.erase(it);
	}
}


int QPSolver::nrTasks() const
{
	return static_cast<int>(tasks_.size());
}


void QPSolver::solver(const std::string& name)
{
	solver_ = std::unique_ptr<GenQPSolver>(createQPSolver(name));
	solver_->updateSize(data_.nrVars_, maxEqLines_, maxInEqLines_, maxGenInEqLines_);
}


void QPSolver::resetTasks()
{
	tasks_.clear();
}


const SolverData& QPSolver::data() const
{
	return data_;
}


SolverData& QPSolver::data()
{
	return data_;
}


const Eigen::VectorXd& QPSolver::result() const
{
	return solver_->result();
}


Eigen::VectorXd QPSolver::alphaDVec() const
{
	return solver_->result().head(data_.totalAlphaD_);
}


Eigen::VectorXd QPSolver::alphaDVec(int rIndex) const
{
	return solver_->result().segment(data_.alphaDBegin_[rIndex],
		data_.alphaD_[rIndex]);
}


Eigen::VectorXd QPSolver::lambdaVec() const
{
	return solver_->result().segment(data_.lambdaBegin(), data_.totalLambda_);
}


Eigen::VectorXd QPSolver::lambdaVec(int cIndex) const
{
	return solver_->result().segment(data_.lambdaBegin_[cIndex],
		data_.lambda_[cIndex]);
}


int QPSolver::contactLambdaPosition(const ContactId& cId) const
{
	int pos = 0;

	for(const BilateralContact& bc: data_.allContacts())
	{
		if(bc.contactId == cId)
		{
			return pos;
		}

		for(std::size_t i = 0; i < bc.r1Points.size(); ++i)
		{
			pos += bc.nrLambda(int(i));
		}
	}

	return -1;
}


void QPSolver::preUpdate(const std::vector<rbd::MultiBody>& mbs,
												std::vector<rbd::MultiBodyConfig>& mbcs)
{
	data_.computeNormalAccB(mbs, mbcs);
	for(std::size_t i = 0; i < constr_.size(); ++i)
	{
		constr_[i]->update(mbs, mbcs, data_);
	}

	for(std::size_t i = 0; i < tasks_.size(); ++i)
	{
		tasks_[i]->update(mbs, mbcs, data_);
	}

	solver_->updateMatrix(tasks_, eqConstr_, inEqConstr_, genInEqConstr_,
		boundConstr_);
}


void QPSolver::postUpdate(const std::vector<rbd::MultiBody>& /* mbs */,
	std::vector<rbd::MultiBodyConfig>& mbcs, bool success)
{
	if(success)
	{
		for(std::size_t r = 0; r < mbcs.size(); ++r)
		{
			rbd::vectorToParam(
				solver_->result().segment(data_.alphaDBegin_[r], data_.alphaD_[r]),
				mbcs[r].alphaD);
		}

		// don't write contact force to the structure since contact force are used
		// to compute C vector.
	}
}


} // namespace qp

} // namespace tasks
